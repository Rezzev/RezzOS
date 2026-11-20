import questionary
import os
import subprocess
from pathlib import Path

repo = Path(__file__).resolve().parent
kernel_175 = "https://github.com/Rezzev/RezzOS/releases/download/1.75/bzImage"
repo_lick = "https://github.com/Rezzev/RezzOS.git"
# FUNCTIONS

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

start_vote = questionary.confirm("Start build script?").ask()
print("Weclome to build script for RezzOS")
## LAUNCH FUNCTIONS
binary_use()
kernel_use()
