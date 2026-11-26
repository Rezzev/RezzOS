import questionary
import os
import subprocess
import shutil
from pathlib import Path

repo = Path(__file__).resolve().parent
kernel_175 = "https://github.com/Rezzev/RezzOS/releases/download/1.75/bzImage"
repo_link = "https://github.com/Rezzev/RezzOS.git"
# FUNCTIONS

## add packages
def add_pkgs():
    add_packages = questionary.confirm(
        "Do you want to add additional packages?"
    )
    if add_packages:
        print("Enter the package names, separated by spaces, and ensure that they exist in the repositories alpine linux.")
        print("Example: firefox vim python3")
        pkgs = input("Enter: ")
        with open("build.sh", "r", encoding="utf-8") as file:
            lines = file.readlines()

        prefix = '    fakeroot ./sbin/apk.static --root "$ROOTFS_DIR" -X http://dl-cdn.alpinelinux.org/alpine/v3.20/main -X http://dl-cdn.alpinelinux.org/alpine/v3.20/community --allow-untrusted --no-scripts add rofi zenity menu-cache gvfs mesa-dri-gallium gtk+3.0 '
        for i, line in enumerate(lines):
            if line.startswith(prefix):
                lines[i] = line.rstrip("\n") + " " + pkgs + "\n"
                break

        with open("build.sh", "w", encoding="utf-8") as file:
            file.writelines(lines)



# setup deeps
def setup_dependencies():
    whether_setup_deeps = questionary.confirm(
        "You want to install dependencies for the build(if you ise alpine)").ask()
    if whether_setup_deeps:
        setup_deeps = subprocess.run(["apk add build-base flex bison linux-headers elfutils-dev openssl-dev perl bc cpio gzip wget tar grub-bios xorriso bash qemu-img fakeroot gtk+3.0-dev gtksourceview-dev e2fsprogs"], text=True, capture_output=True, check=True)
        print(setup_deeps.stdout)

# Use binary or compile ?
def binary_use():
    whether_use_binaries = questionary.select(
        "Whether to use binaries instead of compiling?",
        choices=["Use binary", "Compile programms"]
    ).ask()
    if whether_use_binaries == "Use binary":
        rm_source = subprocess.run(["rm", "-rf", repo / "src" / "rezzinstall.c", repo / "src" / "rezzconfig.c", repo / "src" / "rcomp.c"], check=True, text=True, capture_output=True)
        print(rm_source.stdout)
        mv_binary = subprocess.run(f'mv "{repo}/binary/"* "{repo}/usr/bin/"', shell=True, check=True, capture_output=True, text=True)
        print(mv_binary.stdout)


    elif whether_use_binaries == "Compile Programms":
        print("accepted")

# select kernel
def kernel_use():
    which_kernel_use = questionary.select(
        "Which kernel to use?",
        choices=["Download RezzOS kernel", "Build RezzOS kernel", "Select my kernel"]
    ).ask()


    if which_kernel_use == "Select my kernel":
        questionary.print("Enter the full path!And kernel name must be: |bzImage|", style="bold fg:ansired")
        print("Specify the path to your kernel:")
        kernel_path = input("Enter:")
        copy_kernel = subprocess.run(["cp", kernel_path, repo], capture_output=True, text=True, check=True)
        print(copy_kernel.stdout)


    elif which_kernel_use == "Download RezzOS kernel":
        print("Download RezzOS kernel from github...")
        download_kernel = subprocess.run(["wget", "-P", repo, kernel_175], capture_output=True, text=True, check=True)
        print(download_kernel.stdout)

# set terminal
def get_linux_terminal():
    env_term = os.environ.get("TERMINAL")
    if env_term and shutil.which(env_term):
        return env_term


    terminals = [
        "x-terminal-emulator",
        "gnome-terminal",
        "konsole",
        "xfce4-terminal",
        "mate-terminal",
        "lxterminal",
        "alacritty",
        "kitty",
        "terminator",
        "xterm"
    ]

    for term in terminals:
        if shutil.which(term):
            return term

    return None

# build
def start_build(repo: Path):
    terminal = get_linux_terminal()
    if not terminal:
        print("Not found terminal")
        return
    confirm_build = questionary.confirm("Start build?").ask()
    if confirm_build:
        script_path = str(repo / "build.sh")
        flag = "-e" if terminal in ("konsole", "xterm", "alacritty", "lxterminal") else "--"

        subprocess.Popen([terminal, flag, "bash", script_path])
# weclome
def welcome():
    os.system("clear")
    questionary.print(" Welcome to buid script! \n Here you can configure your system even before \n the ISO is built—include the necessary packages, \n your own kernel, and more.")
    start_vote = questionary.confirm("Start build script?").ask()

## LAUNCH FUNCTIONS
try:
    welcome()
    add_pkgs()
    setup_dependencies()
    binary_use()
    kernel_use()
    start_build(repo)
except Exception as e:
    print(f"Error{e}")
