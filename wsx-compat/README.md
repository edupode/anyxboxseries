# Ponte de compatibilidade Direct3D 12

O adaptador x64 encaminha duas funções de root signatures para o Direct3D 12 do Windows. O executável de teste gera os seus próprios descritores e passa-os pela DLL.

| Exportação | Chamada encontrada na build |
|---|---|
| D3D12CreateRootSignatureDeserializer | amd_fidelityfx_upscaler_xbox.dll usa o mesmo GUID da interface de desserialização do Windows. |
| D3D12SerializeVersionedRootSignature | XboxBvhBuilder.dll passa um descritor versionado. A chamada acrescenta os bits de flags 0x03000000. |

O cabeçalho público d3d12.h do Windows SDK instalado define as flags de 0x00000001 a 0x00000800. Num descritor 1.1 gerado para o teste, o Direct3D do Windows devolveu 0x80070057 ao receber 0x03000000 e aceitou o mesmo descritor com flags a zero. O adaptador devolve E_NOTIMPL quando recebe um dos bits Xbox observados e regista as flags e o HRESULT em shim_log.txt, junto ao executável chamador. Nas chamadas encaminhadas, o HRESULT vem do Direct3D do Windows.

## Compilar e testar

Usa a consola x64 do Visual Studio com CMake e Ninja no PATH e um Windows SDK com suporte para root signatures 1.2 no cabeçalho. Na pasta wsx-compat, executa os comandos abaixo. Esta configuração foi validada em Release.

~~~powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TRY_COMPILE_CONFIGURATION=Release -DCMAKE_POLICY_DEFAULT_CMP0141=NEW -DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix .
~~~

O binário é instalado em bin/d3d12_xs.dll. O teste verifica a serialização e desserialização de descritores 1.0 e 1.1, o erro das flags Xbox e o tratamento de dados inválidos. A execução de validação foi registada no ficheiro shim_log.txt.

## Testar o arranque gráfico

O executável de teste importa as duas funções de d3d12_xs.dll pela tabela de importações PE. Inicializa o Direct3D 12, compila os shaders, renderiza um triângulo original e espera pela conclusão da fila antes de ler e validar os píxeis.

~~~powershell
.\bin\wsx_graphics_probe.exe
.\bin\wsx_graphics_probe.exe --warp
.\bin\wsx_graphics_probe.exe --frame .\frame.ppm
~~~

A opção --warp seleciona o renderizador por software do Windows. O relatório graphics-probe.txt contém os resultados em hardware e WARP. A imagem frame-verificado.png vem do buffer lido no teste em hardware.

## Arranque de módulos

O binário wsx_bootstrap.exe abre wsx_boot_graphics.dll, cria o dispositivo Direct3D 12 e chama a entrada WsxStartV1 com esse dispositivo. Ao receber uma pasta em --build, a DLL confirma que consegue abrir MicrosoftGame.Config e depois renderiza e verifica um frame.

~~~powershell
.\bin\wsx_bootstrap.exe
.\bin\wsx_bootstrap.exe --build 'C:\Users\edupo\Downloads\GTA6.Xbox.v1.0.2.1-CYBERLEEK'
~~~

Um módulo compatível pode ser escolhido com --module caminho.dll. O contrato em boot_api.h fornece referências válidas durante a chamada. O módulo conclui o trabalho antes de devolver um HRESULT. O código de saída zero indica que a DLL escolhida concluiu o arranque. O arranque do motor do GTA VI requer código do jogo.

## Dependências da build

As chamadas de XMemAlloc e XMemFree passam um segundo argumento de 64 bits. No upscaler, o valor inicial observado é 0xA8004000. A implementação desktop precisaria de preservar o tipo de memória e as garantias de alinhamento que a build pede.

Em XboxBvhBuilder.dll, a chamada de XMemVirtualQuery usa três argumentos e o chamador consulta bits no resultado a partir do deslocamento 0x30. O layout e a semântica desse campo, bem como os de XMemGetWorkingSetStatistics, continuam por validar. O arranque do jogo também depende do game_scarlett_master.exe, ausente nesta build.
