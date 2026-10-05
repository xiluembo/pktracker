# Builds e UI mobile

Os cinco workflows executam em push, pull request e manualmente, usando Qt
**6.8.3** e **aqtinstall 3.3.0**. As variantes desktop e mobile são compiladas
separadamente, com testes, nas matrizes:

| Plataforma | Compiladores |
| --- | --- |
| Windows x64 (`windows-2022`) | MSVC 2022, MinGW-w64 GCC 13.1, LLVM-MinGW Clang 17.0.6 |
| Linux x64 (`ubuntu-24.04`) | GCC e LLVM/Clang (com libstdc++) |
| Android | NDK r26b / Clang, arm64-v8a e x86_64 |

Cada compilador Windows usa seu próprio pacote Qt e toolchain oficial via aqt.
No Linux, GCC e Clang usam o pacote `linux_gcc_64`, com a mesma ABI libstdc++.
Os artefatos Windows incluem DLLs e plugins via `windeployqt`.
Os arquivos Linux contêm os executáveis; exigem Qt 6.8.3 Widgets e Multimedia
no sistema (ou `LD_LIBRARY_PATH` apontando para o diretório `gcc_64/lib` do aqt).
Os APKs Android usam SDK 35, API mínima 28, Java 17 e assinatura de debug
para instalação/testes; publicação em loja requer assinatura de release.

## Compilar localmente

```sh
python -m pip install aqtinstall==3.3.0
python -m aqt install-qt linux desktop 6.8.3 linux_gcc_64 -m qtmultimedia -O Qt
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$PWD/Qt/6.8.3/gcc_64" -DPKTRACKER_MOBILE_UI=ON
cmake --build build --parallel 2
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
```

No Linux, instale também Ninja, compilador C++, headers OpenGL e xkbcommon.
Use `-DPKTRACKER_MOBILE_UI=OFF` para a UI desktop original (padrão no desktop).
No Android a opção mobile está ligada por padrão. Use `-DBUILD_TESTING=OFF`
para omitir os testes. A UI mobile afeta o Planner; o wizard de importação também recebe páginas roláveis e controles maiores,
mas não é empacotado como aplicativo Android separado.

## Interação mobile

- **Tools** abre a paleta. Ao escolher um item, o diálogo fecha e a dica informa
  qual ferramenta está ativa. Toque no mapa para inserir; use Select / Preview
  para selecionar, ouvir e arrastar itens.
- **Pan** permite arrastar o mapa sem editar. Desative Pan para voltar à edição;
  selecionar outra ferramenta também retorna à edição.
- **Rotate**, **Delete**, **Play**, **Pause** e **Stop** ficam junto à borda inferior.
- **Settings** reúne camada, orientação, tempo por tile e adição de camadas.
- **Menu** reúne importação/exportação, MIDI, copiar/colar e opções de visualização.

Os controles têm alvos de toque de pelo menos 48 pixels lógicos, nomes acessíveis,
foco por teclado, cores nativas do sistema e texto explícito. A janela aceita
largura de 320 pixels e mantém o mapa como área principal. A implementação
compartilha ações, modelo, serialização, áudio e simulação com a UI desktop.
