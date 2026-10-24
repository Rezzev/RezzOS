# Installing RezzOS

*This guide is intended for first-time and non-expert users.*

The installation of RezzOS is a relatively quick and straightforward process. Below is the recommended procedure for setting up the operating system on your computer.

---

## Step 1: Requirements

Before starting the installation, ensure that your system meets the following requirements:

- A USB flash drive with at least 512 MB of storage capacity
- A computer equipped with a 64-bit processor
- At least 1 GB of RAM
- A working internet connection, if you plan to download the installation image

---

## Step 2: Obtain the RezzOS installation image

You can install RezzOS in one of the following ways:

- Download a prebuilt ISO image from the official GitHub Releases page
- Or build the ISO image manually by following the instructions in `BUILD.md`

Choose the method that best suits your needs and available resources.

---

## Step 3: Write the ISO image to the USB flash drive

Once the ISO image has been downloaded, write it to a USB flash drive using a suitable tool. Common options include:

- Rufus (Windows)
- Ventoy
- balenaEtcher
- Other compatible bootable USB creators

Ensure that the USB drive is correctly prepared before proceeding.

---

## Step 4: Boot the system from the USB drive

1. Insert the USB flash drive into the computer.
2. Restart the device.
3. Enter the BIOS/UEFI setup or the boot menu.
4. Select the USB drive as the boot device.
5. From the GRUB menu, choose the first available option to start the system.

After the system loads, the RezzOS environment will become available.

---

## Step 5: Install RezzOS to the hard disk

After the system has booted successfully:

1. Right-click on any empty area of the desktop.
2. In the context menu, select "Install On Disk".
3. Follow the on-screen instructions provided by the installer.
4. Enter the required information in the appropriate fields.
5. Select the desired installation settings.
6. Confirm the installation and wait for the process to complete.

Please note that the installer may ask you to choose the target disk or partition. Carefully review the selected destination to avoid data loss.

---

## Step 6: Finalize the installation

When the installation process is complete, restart the computer. If prompted, remove the USB flash drive before booting from the internal storage.

Once the system starts successfully, you will be able to use RezzOS normally.

---

## Important Notes

- This guide is intended for general-purpose users and follows a simplified installation flow.
- Before continuing, review all installation prompts carefully.
- If additional clarification is needed, refer to the project documentation and technical resources provided in the repository.
- Since the project is still in development, you may encounter bugs and issues during installation or within the installer itself.

We recommend proceeding with caution when choosing the target drive and installation options.

Thank you for choosing RezzOS.
