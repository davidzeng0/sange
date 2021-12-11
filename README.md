# sange
FFmpeg powered audio player in node.js

### prerequisites
Only compiles on Linux or WSL2+.

1. node.js
2. cmake
3. c++ compiler
4. ffmpeg
5. libuv
6. libopus

### install
```bash
# dependencies
apt install cmake g++ gcc libavcodec-dev libavcodec58 libavformat-dev libavformat58 libavutil-dev libavutil56 libavfilter7 libavfilter-dev libswresample-dev libswresample3 libuv1-dev libopus-dev

# optional
apt install ninja-build

# install
npm i git://github.com/ilikdoge/sange.git
```

### ffmpeg

Building ffmpeg from source may boost performance

```bash
apt install pkg-config libssl-dev libmp3lame-dev libopus-dev libvorbis-dev

git clone --depth 0 https://github.com/FFmpeg/FFmpeg
cd FFmpeg
./configure --arch=amd64 --disable-stripping --enable-openssl --enable-libmp3lame --enable-libopus --enable-libvorbis --enable-shared --enable-nonfree
make -j $(nproc)
make install
ldconfig
```