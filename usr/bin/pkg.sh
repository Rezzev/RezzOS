#!/bin/sh

# Directory where .apk files will be saved (for cache or offline transfer)
PKG_DIR="/mnt/disk/packages"

usage() {
    echo "Usage: $0 <install|remove|search|list> [package]"
    exit 1
}

# TODO COMPLIANCE 1: Validate root privileges before doing any modifications
check_root() {
    if [ "$(id -u)" -ne 0 ]; then
        echo "Error: This operation requires root privileges. Please run with sudo."
        exit 1
    fi
}

# TODO COMPLIANCE 2: Check available disk space before starting the download
# Requires at least 50MB free space as a safe threshold for common packages
check_disk_space() {
    # Get free space of target directory parent in KB
    target_dir=$(dirname "$PKG_DIR")
    mkdir -p "$target_dir"
    
    FREE_KB=$(df -k "$target_dir" | awk 'NR==2 {print $4}')
    # 51200 KB = 50 MB
    if [ "$FREE_KB" -lt 51200 ]; then
        echo "Error: Insufficient disk space on $target_dir. Less than 50MB available."
        exit 1
    fi
}

# Check that at least one argument is provided
[ -z "$1" ] && usage

case "$1" in
    install)
        [ -z "$2" ] && usage
        PKG="$2"
        
        check_root
        check_disk_space
        
        echo "Creating package directory: $PKG_DIR"
        mkdir -p "$PKG_DIR"
        
        echo "Downloading package '$PKG' and ALL of its dependencies..."
        if ! apk fetch --recursive --output "$PKG_DIR" "$PKG"; then
            echo "Error: Failed to find or download package '$PKG'"
            exit 1
        fi
        
        echo "Installing packages from the local directory..."
        if apk add --repository "$PKG_DIR" --allow-untrusted "$PKG"; then
            echo "Package '$PKG' has been successfully installed!"
            # Record to history, matching original script logic
            echo "$PKG" >> "$PKG_DIR/installed.list"
        else
            echo "Error: Failed to install package '$PKG'"
            exit 1
        fi
        ;;

    remove)
        # TODO COMPLIANCE 3: Clean implementation of uninstall feature
        [ -z "$2" ] && usage
        PKG="$2"
        
        check_root
        
        echo "Uninstalling package '$PKG' cleanly..."
        if apk del "$PKG"; then
            echo "Package '$PKG' has been successfully removed."
            
            # Remove the package name from installed.list history file if it exists
            if [ -f "$PKG_DIR/installed.list" ]; then
                # Filter out the removed package line
                sed -i "/^${PKG}$/d" "$PKG_DIR/installed.list"
            fi
        else
            echo "Error: Failed to uninstall package '$PKG'"
            exit 1
        fi
        ;;

    search)
        [ -z "$2" ] && usage
        echo "Searching for package '$2' in official repositories..."
        apk search -v "$2"
        ;;

    list)
        if [ -f "$PKG_DIR/installed.list" ]; then
            echo "Packages installed via this script:"
            cat "$PKG_DIR/installed.list"
        else
            echo "Installation history is empty (file $PKG_DIR/installed.list not found)"
        fi
        ;;

    *)
        usage
        ;;
esac
