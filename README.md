
[![codecov](https://codecov.io/github/Arrooy/LinuxCam/branch/develop/graph/badge.svg?token=NWVE3AC940)](https://codecov.io/github/Arrooy/LinuxCam)

## Cloning the repository:
```console
git clone --recurse-submodules -j8 https://github.com/Arrooy/LinuxCam.git
```
### or
```console
1. git clone https://github.com/Arrooy/LinuxCam.git
2. git submodules init
3. git submodule update
```
## Requirements
### libturbojpeg:
```console
    sudo apt get install nasm
```
### dlib:
```console
    sudo apt install libjxl-dev
    sudo apt-get install libopenblas-dev liblapack-dev
    sudo apt install libavdevice-dev libavfilter-dev libavformat-dev
    sudo apt install libavcodec-dev libswresample-dev libswscale-dev
    sudo apt install libavutil-dev
```
### X11:
    ```console
        sudo apt-get install libxinerama-dev
    sudo apt install libxcursor-dev
    sudo apt-get install libxi-dev
```
### cuda:
You need cudnn 9 and cuda 12
Install it using:
cuda: `https://developer.nvidia.com/cuda-downloads`
cudnn: `https://developer.nvidia.com/cudnn-downloads`
I tested it with version cudnn9-cuda-12
## Compilation
Cuda 12 supports gcc <= 12 !!
if you have gcc13, you need to install a older version and override them:

```console
export CC="/usr/bin/gcc-12" && export CXX="/usr/bin/g++-12"
```
```console
mkdir -p build && cd build && cmake .. && make -j$(nproc)
```
## Install virtual loopback
After cmake has run, if you see `v4l2loopback module NOT installed.`

Install if not installed yet with:
```console
sudo make install_v4l2loopback
```
Create a virtual webcam
```console
sudo modprobe v4l2loopback exclusive_caps=1 video_nr=8 card_label="VirtualWebcam"
```
Important to set video_nr to a free video device number.

Check with `ls /dev/ | grep video`
# Execution
You can manually modify the configuration file `config.yaml`.

To execute, run: `./LinuxFace`
