![GitHub release (latest by date)](https://img.shields.io/github/v/release/FS-make-simple/bcm)
![GitHub Release Date](https://img.shields.io/github/release-date/FS-make-simple/bcm)
![GitHub repo size](https://img.shields.io/github/repo-size/FS-make-simple/bcm)
![GitHub all releases](https://img.shields.io/github/downloads/FS-make-simple/bcm/total)
![GitHub](https://img.shields.io/github/license/FS-make-simple/bcm)

# BCM

### Description
BCM is a high-performance file compressor that utilizes advanced context modeling techniques to achieve a very high compression ratio. All in all, it's like a big brother of the BZIP2.

### Quick Benchmark
[ENWIK8][1] | Compressed Size   |
------------|-------------------|
Original    | 100,000,000 bytes |   
GZIP -9     | 36,445,248 bytes  |
BZIP2 -9    | 29,008,758 bytes  |
7-Zip Ultra | 24,864,804 bytes  |
BCM -b100   | 20,789,667 bytes  |

[1]:http://mattmahoney.net/dc/text.html

### Depends

Libsais:
* https://github.com/IlyaGrebnov/libsais
* https://github.com/FS-make-simple/libsais

### Author
Ilya Muravyov

### Thanks
Special thanks to Ilya Grebnov, Yuta Mori, Matt Mahoney, Eugene Shelwien, Alexander Rhatushnyak, Przemyslaw Skibinski, Malcolm Taylor and LovePimple.
