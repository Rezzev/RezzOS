import questionary
import os
import subprocess
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
        print("Specify the path to your kernel:")
        kernel_path = input("Enter:")
        copy_kernel = subprocess.run(["cp", kernel_path, repo], capture_output=True, text=True, check=True)
        print(copy_kernel.stdout)


    elif which_kernel_use == "Download RezzOS kernel":
        print("Download RezzOS kernel from github...")
        download_kernel = subprocess.run(["wget", "-P", repo, kernel_175], capture_output=True, text=True, check=True)
        print(download_kernel.stdout)
# build
def start_build():
    confirm_build = questionary.confirm("Start build?").ask()
    if confirm_build:
        start = subprocess.run(["bash", "build.sh"], shell=True, capture_output=True, text=True, check=True)
        print(start.stdout)

start_vote = questionary.confirm("Start build script?").ask()
print("Weclome to build script for RezzOS")
## LAUNCH FUNCTIONS
add_pkgs()
setup_dependencies()
binary_use()
kernel_use()
start_build()
