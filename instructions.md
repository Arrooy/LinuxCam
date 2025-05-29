libturbojpeg:
    sudo apt get install nasm

dlib:
    sudo apt install libjxl-dev
    sudo apt-get install libopenblas-dev liblapack-dev
    sudo apt install libavdevice-dev libavfilter-dev libavformat-dev
    sudo apt install libavcodec-dev libswresample-dev libswscale-dev
    sudo apt install libavutil-dev

X11:
    sudo apt-get install libxinerama-dev
    sudo apt install libxcursor-dev
    sudo apt-get install libxi-dev

cuda:
    sudo apt install nvidia-cuda-toolkit
    sudo apt-get -y install cudnn9-cuda-12

ncnn - https://github.com/Tencent/ncnn/wiki/how-to-build:

    sudo apt install build-essential git cmake libprotobuf-dev protobuf-compiler libomp-dev libopencv-dev

    Clone repo
    create build and execute:
        cmake -DCMAKE_BUILD_TYPE=Release -DNCNN_VULKAN=ON -DNCNN_BUILD_EXAMPLES=ON -GNinja ..
