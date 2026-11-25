//! Numeric, command-ordered readings of the original probe cache's two atlases.
use std::sync::{
    Mutex, OnceLock,
    atomic::{AtomicU64, Ordering},
};
use windows::Win32::Graphics::Direct3D12::*;
use windows::core::Interface;

use crate::{descriptor::host::HostHeap, host};

const MAX_HELD: u64 = 128 << 20;
static HELD: AtomicU64 = AtomicU64::new(0);

fn frames() -> &'static Vec<u64> {
    static FRAMES: OnceLock<Vec<u64>> = OnceLock::new();
    FRAMES.get_or_init(|| {
        std::env::var("VICTORY_IRRADIANCE_FRAMES")
            .unwrap_or_default()
            .split(',')
            .take(64)
            .filter_map(|s| s.trim().parse().ok())
            .collect()
    })
}

pub(crate) fn configured() -> bool {
    !frames().is_empty() && !host::late()
}

pub(crate) fn enabled() -> bool {
    configured() && frames().contains(&(crate::queue::presented() + 1))
}

pub(crate) struct Bound {
    pub resource: ID3D12Resource,
    heap: usize,
    native_heap: usize,
    slot: u32,
    point: &'static str,
    format: i32,
}

pub(crate) fn bound(hash: &str, base: u32, heap: &HostHeap, facade: usize) -> Vec<Bound> {
    let points: &[(u32, &str, i32)] = match hash {
        "a53a7977fe762268" => &[(104, "shade-radiance", 26)],
        "c519f7a7946e3702" => &[(80, "gather-copy-distance", 54)],
        "d1644714f77a1cc1" => &[
            (8, "radiance-copy-input", 26),
            (16, "radiance-copy-output", 26),
        ],
        "0a5d4f9047b3dce4" => &[
            (8, "integrate-distance", 54),
            (16, "integrate-radiance", 26),
        ],
        _ => return vec![],
    };
    if base % 32 != 0 {
        return vec![];
    }
    points
        .iter()
        .filter_map(|&(offset, point, format)| {
            let slot = (base / 32).checked_add(offset / 8)?;
            Some(Bound {
                resource: heap.retained_native_slot(slot)?,
                heap: facade,
                native_heap: heap.heap.as_raw() as usize,
                slot,
                point,
                format,
            })
        })
        .collect()
}

#[derive(Clone, Copy)]
pub(crate) enum Args<'a> {
    Direct([u32; 3]),
    Indirect(&'a ID3D12Resource, u64),
}

enum Groups {
    Direct([u32; 3]),
    Indirect(ID3D12Resource),
}

struct Budget(u64);
impl Drop for Budget {
    fn drop(&mut self) {
        HELD.fetch_sub(self.0, Ordering::Relaxed);
    }
}

pub(crate) struct Sample {
    _budget: Budget,
    frame: u64,
    slot: u32,
    point: &'static str,
    native: usize,
    heap: usize,
    native_heap: usize,
    before_submit: std::sync::atomic::AtomicU8,
    image: ID3D12Resource,
    foot: D3D12_SUBRESOURCE_FOOTPRINT,
    groups: Groups,
}

pub(crate) fn record(
    list: &ID3D12GraphicsCommandList,
    binding: &Bound,
    before: u32,
    args: Args<'_>,
) -> Option<Sample> {
    if !enabled() {
        return None;
    }
    let frame = crate::queue::presented() + 1;
    // SAFETY: this is the native object retained by the actual bound heap
    // slot and by the list's state tracker. Never follow a title address.
    let desc = unsafe { binding.resource.GetDesc() };
    if desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D
        || desc.SampleDesc.Count != 1
        || desc.DepthOrArraySize != 1
        || desc.MipLevels != 1
        || desc.Format.0 != binding.format
    {
        return None;
    }
    let bytes_per = match desc.Format.0 {
        26 => 4,
        54 => 2,
        _ => return None,
    };
    let pitch = desc.Width.checked_mul(bytes_per)?.checked_add(255)? & !255;
    let bytes = pitch
        .checked_mul(u64::from(desc.Height))?
        .checked_add(65536)?;
    if bytes > 17 << 20 {
        return None;
    }
    static COUNTS: Mutex<(u64, Vec<&'static str>)> = Mutex::new((0, vec![]));
    let mut counts = COUNTS.lock().unwrap_or_else(|e| e.into_inner());
    if counts.0 != frame {
        counts.0 = frame;
        counts.1.clear();
    }
    if counts.1.contains(&binding.point) && !binding.point.starts_with("test-") {
        return None;
    }
    if counts.1.iter().filter(|&&p| p == binding.point).count() >= 2 {
        return None;
    }
    HELD.fetch_update(Ordering::Relaxed, Ordering::Relaxed, |held| {
        held.checked_add(bytes).filter(|&x| x <= MAX_HELD)
    })
    .ok()?;
    let budget = Budget(bytes);
    let host = host::host()?;
    let groups = match args {
        Args::Direct(xyz) => Groups::Direct(xyz),
        Args::Indirect(buffer, at) => {
            // The original ExecuteIndirect has just read this very scratch
            // argument. Its caller established INDIRECT_ARGUMENT and restores
            // it to COMMON only after our sample.
            let d = unsafe { buffer.GetDesc() };
            if d.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER || at.checked_add(12)? > d.Width {
                return None;
            }
            Groups::Indirect(host.read_back_buffer(12)?)
        }
    };
    let before = D3D12_RESOURCE_STATES(before as i32);
    let src = D3D12_RESOURCE_STATE_COPY_SOURCE;
    let tex = &binding.resource;
    unsafe {
        if before != src {
            list.ResourceBarrier(&[host::transition(tex, before, src)]);
        }
    }
    let image = crate::capture::read_back(&host.device, list, tex);
    unsafe {
        if before != src {
            list.ResourceBarrier(&[host::transition(tex, src, before)]);
        }
    }
    let (image, footprint) = image?;
    if let (Args::Indirect(from, at), Groups::Indirect(to)) = (args, &groups) {
        unsafe {
            list.ResourceBarrier(&[host::transition(
                from,
                D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT,
                src,
            )]);
            list.CopyBufferRegion(to, 0, from, at, 12);
            list.ResourceBarrier(&[host::transition(
                from,
                src,
                D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT,
            )]);
        }
    }
    counts.1.push(binding.point);
    Some(Sample {
        _budget: budget,
        frame,
        slot: binding.slot,
        point: binding.point,
        native: tex.as_raw() as usize,
        heap: binding.heap,
        native_heap: binding.native_heap,
        before_submit: 0.into(),
        image,
        foot: footprint.Footprint,
        groups,
    })
}

fn component(bits: u32, mantissa: u32) -> f32 {
    let exponent = bits >> mantissa;
    let fraction = bits & ((1 << mantissa) - 1);
    match exponent {
        0 => (fraction as f32) * 2f32.powi(-14 - mantissa as i32),
        31 if fraction == 0 => f32::INFINITY,
        31 => f32::NAN,
        _ => f32::from_bits(((exponent + 112) << 23) | (fraction << (23 - mantissa))),
    }
}

#[derive(Default, Debug)]
struct Stats {
    texels: u64,
    positive: u64,
    negative: u64,
    nonfinite: u64,
    peak: f32,
    hash: blake3::Hasher,
}

fn measure(bytes: &[u8], f: &D3D12_SUBRESOURCE_FOOTPRINT) -> Option<Stats> {
    let bpp = match f.Format.0 {
        26 => 4,
        54 => 2,
        _ => return None,
    };
    let mut stats = Stats::default();
    let mut finite = |values: &[f32]| {
        stats.texels += 1;
        stats.nonfinite += u64::from(values.iter().any(|v| !v.is_finite()));
        stats.negative += u64::from(values.iter().any(|v| v.is_finite() && *v < 0.));
        stats.positive += u64::from(values.iter().any(|v| v.is_finite() && *v > 0.));
        for &v in values {
            if v.is_finite() {
                stats.peak = stats.peak.max(v);
            }
        }
    };
    for y in 0..usize::try_from(f.Height).ok()? {
        let at = y.checked_mul(f.RowPitch as usize)?;
        let row = bytes.get(at..at.checked_add((f.Width as usize).checked_mul(bpp)?)?)?;
        stats.hash.update(row);
        for pixel in row.chunks_exact(bpp) {
            if bpp == 4 {
                let p = u32::from_le_bytes(pixel.try_into().ok()?);
                finite(&[
                    component(p & 2047, 6),
                    component((p >> 11) & 2047, 6),
                    component(p >> 22, 5),
                ]);
            } else {
                let p = u16::from_le_bytes(pixel.try_into().ok()?);
                let value = component(u32::from(p & 0x7fff), 10);
                finite(&[if p & 0x8000 != 0 { -value } else { value }]);
            }
        }
    }
    Some(stats)
}

impl Sample {
    fn slot_receipt(&self) -> u8 {
        match crate::descriptor::captured_slot_still_shows(
            self.heap,
            self.native_heap,
            self.slot,
            self.native,
        ) {
            None => 0,
            Some(false) => 1,
            Some(true) => 2,
        }
    }

    pub(crate) fn before_submit(&self) {
        self.before_submit
            .store(self.slot_receipt(), Ordering::Relaxed);
    }
    fn read(&self) -> Option<([u32; 3], Stats)> {
        // Invoked only after every physical group in this synchronous submission
        // passed Signal, wait, fence-value and device-health checks.
        let xyz = match &self.groups {
            Groups::Direct(xyz) => *xyz,
            Groups::Indirect(buffer) => unsafe {
                let mut ptr = std::ptr::null_mut();
                if buffer.Map(0, None, Some(&mut ptr)).is_err() || ptr.is_null() {
                    return None;
                }
                let words = ptr.cast::<[u32; 3]>().read_unaligned();
                buffer.Unmap(0, None);
                words
            },
        };
        let mut ptr = std::ptr::null_mut();
        let stats = unsafe {
            let size = self.image.GetDesc().Width as usize;
            if self.image.Map(0, None, Some(&mut ptr)).is_err() || ptr.is_null() {
                return None;
            }
            let result = measure(
                std::slice::from_raw_parts(ptr.cast::<u8>(), size),
                &self.foot,
            );
            self.image.Unmap(0, None);
            result
        };
        Some((xyz, stats?))
    }

    pub(crate) fn report(&self, submission: u64, fence: u64, replays: usize) {
        if let Some((xyz, s)) = self.read() {
            let matches_at_submit = self.before_submit.load(Ordering::Relaxed) == 2;
            let matches_at_fence = self.slot_receipt() == 2;
            tracing::info!(recorded_frame=self.frame, observed_frame=crate::queue::presented()+1, submission,fence,replays,point=self.point,slot=self.slot,native=self.native,matches_at_submit,matches_at_fence,format=self.foot.Format.0,width=self.foot.Width,height=self.foot.Height,?xyz,texels=s.texels,positive=s.positive,negative=s.negative,nonfinite=s.nonfinite,peak=s.peak,blake3=%s.hash.finalize(),"retired irradiance atlas");
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::mem::ManuallyDrop;
    use windows::Win32::Graphics::Dxgi::Common::*;

    #[test]
    #[ignore = "requires a native D3D12 device and VICTORY_IRRADIANCE_FRAMES=1"]
    fn native_atlas_readback_preserves_order_and_indirect_counts() {
        let host = host::host().unwrap();
        let list = host.new_list().unwrap();
        let mut observed = vec![];
        let mut uploads = vec![];
        for (format, width) in [
            (DXGI_FORMAT_R16_FLOAT, 2usize),
            (DXGI_FORMAT_R11G11B10_FLOAT, 4),
        ] {
            let desc = D3D12_RESOURCE_DESC {
                Dimension: D3D12_RESOURCE_DIMENSION_TEXTURE2D,
                Width: 3,
                Height: 2,
                DepthOrArraySize: 1,
                MipLevels: 1,
                Format: format,
                SampleDesc: DXGI_SAMPLE_DESC {
                    Count: 1,
                    Quality: 0,
                },
                Layout: D3D12_TEXTURE_LAYOUT_UNKNOWN,
                ..Default::default()
            };
            let mut texture: Option<ID3D12Resource> = None;
            unsafe {
                host.device
                    .CreateCommittedResource(
                        &D3D12_HEAP_PROPERTIES {
                            Type: D3D12_HEAP_TYPE_DEFAULT,
                            ..Default::default()
                        },
                        D3D12_HEAP_FLAG_NONE,
                        &desc,
                        D3D12_RESOURCE_STATE_COPY_DEST,
                        None,
                        &mut texture,
                    )
                    .unwrap()
            }
            let binding = Bound {
                resource: texture.unwrap(),
                heap: 0,
                native_heap: 0,
                slot: 23,
                point: if width == 2 {
                    "test-distance"
                } else {
                    "test-radiance"
                },
                format: format.0,
            };
            let (args, args_mem) = host.upload_buffer(32).unwrap();
            unsafe {
                std::ptr::copy_nonoverlapping(
                    [2u32, 3, 7].as_ptr().cast::<u8>(),
                    args_mem.add(8),
                    12,
                );
            }
            // The scratch argument used in the real ExecuteIndirect path is
            // in default memory in INDIRECT_ARGUMENT, also at a nonzero offset.
            let indirect = host.uav_buffer(32).unwrap();
            unsafe {
                list.list.ResourceBarrier(&[host::transition(
                    &indirect,
                    D3D12_RESOURCE_STATE_COMMON,
                    D3D12_RESOURCE_STATE_COPY_DEST,
                )]);
                list.list.CopyBufferRegion(&indirect, 8, &args, 8, 12);
                list.list.ResourceBarrier(&[host::transition(
                    &indirect,
                    D3D12_RESOURCE_STATE_COPY_DEST,
                    D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT,
                )]);
            }
            uploads.extend([args, indirect.clone()]);
            for filled in [false, true] {
                let (upload, ptr) = host.upload_buffer(512).unwrap();
                let data = unsafe { std::slice::from_raw_parts_mut(ptr, 512) };
                data.fill(0xab);
                let values = [
                    0,
                    if width == 2 { 0x3c00 } else { 0x3c0 },
                    if width == 2 { 0xbc00 } else { 0x3c0 << 11 },
                    if width == 2 { 0x7c00 } else { 0x7c0 },
                    0,
                    1u32,
                ];
                for (i, &word) in values.iter().enumerate() {
                    let at = (i / 3) * 256 + (i % 3) * width;
                    data[at..at + width]
                        .copy_from_slice(&if filled { word } else { 0 }.to_le_bytes()[..width]);
                }
                let from = D3D12_TEXTURE_COPY_LOCATION {
                    pResource: ManuallyDrop::new(Some(upload.clone())),
                    Type: D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
                    Anonymous: D3D12_TEXTURE_COPY_LOCATION_0 {
                        PlacedFootprint: D3D12_PLACED_SUBRESOURCE_FOOTPRINT {
                            Offset: 0,
                            Footprint: D3D12_SUBRESOURCE_FOOTPRINT {
                                Format: format,
                                Width: 3,
                                Height: 2,
                                Depth: 1,
                                RowPitch: 256,
                            },
                        },
                    },
                };
                let to = D3D12_TEXTURE_COPY_LOCATION {
                    pResource: ManuallyDrop::new(Some(binding.resource.clone())),
                    Type: D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
                    Anonymous: D3D12_TEXTURE_COPY_LOCATION_0 {
                        SubresourceIndex: 0,
                    },
                };
                unsafe {
                    list.list.CopyTextureRegion(&to, 0, 0, 0, &from, None);
                }
                let mut from = from;
                let mut to = to;
                unsafe {
                    ManuallyDrop::drop(&mut from.pResource);
                    ManuallyDrop::drop(&mut to.pResource);
                }
                let sample = record(
                    &list.list,
                    &binding,
                    D3D12_RESOURCE_STATE_COPY_DEST.0 as u32,
                    Args::Indirect(&indirect, 8),
                )
                .unwrap();
                observed.push((width, filled, sample));
                uploads.push(upload);
            }
            uploads.push(binding.resource);
        }
        list.close();
        host.execute(&[&list]);
        for (width, filled, sample) in observed {
            let (xyz, s) = sample.read().unwrap();
            assert_eq!(xyz, [2, 3, 7]);
            assert_eq!(s.texels, 6);
            assert_eq!(
                s.positive,
                if !filled {
                    0
                } else if width == 2 {
                    2
                } else {
                    3
                }
            );
            assert_eq!(s.negative, u64::from(filled && width == 2));
            assert_eq!(s.nonfinite, u64::from(filled));
            assert_eq!(s.peak, if filled { 1. } else { 0. });
            println!(
                "format {} filled {filled}, groups {xyz:?}, texels {}, positive {}, negative {}, nonfinite {}, peak {}",
                sample.foot.Format.0, s.texels, s.positive, s.negative, s.nonfinite, s.peak
            );
        }
        assert!(host.log_messages().is_none());
    }
}
