# ports
This folder contains all of the scripts and patches to build a bentobox userspace.

## Building
You can either run `util/ports.sh` to build all the ports at once, or you can run each file individually from `ports/`

> [!IMPORTANT]
> The current working directory has to be the root of the bentobox repository when running `util/ports.sh` or each port's shell script from `ports/`.

> [!NOTE]
> Musl doesn't have the Linux headers installed by default; you may need to install them yourself from the Linux source code to be able to build `doomgeneric`. Doomgeneric also requires a `.wad` file, which you can get from the Internet Archive.

## Port list
- bash 5.1 (`ports/bash-prebuilt.sh`)
- busybox 1.35.0 (`ports/busybox.sh`)
- doomgeneric (`ports/doomgeneric.sh`)
- figlet (`ports/figlet.sh`)
- neofetch 7.1.0 (`ports/neofetch.sh`)
- vim (`ports/vim.sh`)
- ncurses (`ports/ncurses.sh`)
- tree (`ports/tree.sh`)
