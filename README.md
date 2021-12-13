# sange
FFmpeg powered audio player in node.js

*Sange and Yasha, when attuned by the moonlight and used together, become a very powerful combination.*

### prerequisites
only compiles on Linux or WSL2+.

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

building ffmpeg from source may boost performance<br>
**known issue: the default TLS library (gnutls) that ships with ffmpeg causes an infinite loop. building with OpenSSL fixes that issue.**

```bash
apt install pkg-config libssl-dev libmp3lame-dev libopus-dev libvorbis-dev nasm

git clone --depth 1 https://github.com/FFmpeg/FFmpeg
cd FFmpeg
./configure --arch=amd64 --disable-stripping --enable-openssl --enable-libmp3lame --enable-libopus --enable-libvorbis --enable-shared --enable-nonfree
make -j $(nproc)
make install
ldconfig
```