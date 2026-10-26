# Savanna

**Savanna** é um navegador para Linux baseado no GNOME Web (Epiphany) e no
WebKitGTK. Este projeto mantém a base de navegação do Epiphany e desenvolve uma
interface própria, com grupos de abas, barra lateral, conteúdo salvo e suporte
ao menu global do KDE Plasma.

O programa instalado por este projeto se chama **`savanna`**. Ele usa um ID de
aplicativo próprio (`com.savanna.Browser`) e não instala um executável chamado
`epiphany`.

## Instalação fácil no Linux

### CachyOS e Arch Linux

Instale as ferramentas de compilação e as dependências de interface/WebKit:

```sh
sudo pacman -S --needed base-devel git meson ninja pkgconf \
  gtk4 libadwaita webkitgtk-6.0 dbusmenu-glib wayland libx11
```

Clone o projeto, compile e instale apenas para seu usuário (não precisa de
`sudo` na instalação):

```sh
git clone https://github.com/marcosfantonio/savanna.git savanna-browser
cd savanna-browser
meson setup build --prefix="$HOME/.local" -Ddeveloper_mode=true
meson install -C build --quiet
```

Se o Meson informar que falta uma dependência, instale no `pacman` o pacote de
desenvolvimento correspondente e rode novamente `meson setup`/`meson install`.
O Meson mostra no resumo quais integrações de menu global foram compiladas.

Abra o navegador pelo terminal:

```sh
~/.local/bin/savanna
```

O arquivo `.desktop` instalado também registra o Savanna no lançador de
aplicativos do Plasma. Se o terminal não encontrar comandos instalados em
`~/.local/bin`, adicione essa pasta ao `PATH` ou use o caminho completo acima.

### Outras distribuições

Instale as dependências de desenvolvimento do GTK 4, libadwaita, WebKitGTK 6,
Meson/Ninja e as bibliotecas exigidas pelo projeto; os nomes dos pacotes variam
entre distribuições. Em seguida, use os mesmos comandos de clone, configuração
e instalação acima. O Meson interrompe a configuração e identifica qualquer
dependência ausente.

## Compilar novamente

Na pasta do repositório:

```sh
meson install -C build --quiet
```

Esse comando recompila o que mudou e instala novamente em `~/.local`. Para
executar os testes da interface:

```sh
meson test -C build 'Savanna interface test' --print-errorlogs
```

## Recursos

- Grupos persistentes de abas na barra lateral.
- Histórico, favoritos e conteúdo salvo acessíveis pela interface.
- Integração de abas de dispositivos via Firefox Sync.
- Menu global no Plasma via DBusMenu em Wayland e X11/XWayland, quando o painel
  global-menu estiver habilitado.
- Popover de downloads com altura limitada e rolagem própria.

Consulte [`docs/global-menu.md`](docs/global-menu.md) para detalhes de
compatibilidade e diagnóstico do menu global, e
[`docs/architecture.md`](docs/architecture.md) para a organização da interface
e orientações para alterações futuras. As instruções destinadas a agentes de
código estão em [`AGENTS.md`](AGENTS.md).

## Desenvolvimento

Savanna é software livre, licenciado sob GPL-3.0-or-later. A maior parte dos
módulos ainda usa os nomes internos `ephy-*` da base Epiphany. Mudanças na
interface do Savanna e nas integrações específicas devem seguir a arquitetura e
os comportamentos descritos na documentação acima.
