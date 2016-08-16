/*

BCM - A BWT-based file compressor

Copyright (C) 2008-2016 Ilya Muravyov

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/

#ifndef _MSC_VER
#  define _FILE_OFFSET_BITS 64

#  define _fseeki64 fseeko
#  define _ftelli64 ftello
#  define _stati64 stat

#  ifdef HAVE_GETC_UNLOCKED
#    undef getc
#    define getc getc_unlocked
#  endif
#  ifdef HAVE_PUTC_UNLOCKED
#    undef putc
#    define putc putc_unlocked
#  endif
#endif

#define _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES 1
#define _CRT_SECURE_NO_WARNINGS
#define _CRT_DISABLE_PERFCRIT_LOCKS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef NO_UTIME
#  include <sys/types.h>
#  include <sys/stat.h>

#  ifdef _MSC_VER
#    include <sys/utime.h>
#  else
#    include <utime.h>
#  endif
#endif

#include "divsufsort.h" // libdivsufsort-lite

typedef unsigned char byte;
typedef unsigned short word;
typedef unsigned int uint;
typedef unsigned long long ulonglong;

const char magic[]="BCM!";

FILE* fin;
FILE* fout;

struct Encoder
{
  uint low;
  uint high;
  uint code;

  Encoder()
  {
    low=0;
    high=uint(-1);
    code=0;
  }

  void EncodeBit0(uint p)
  {
#ifdef _WIN64
    low+=((ulonglong(high-low)*p)>>18)+1;
#else
    low+=((ulonglong(high-low)*(p<<(32-18)))>>32)+1;
#endif
    while ((low^high)<(1<<24))
    {
      putc(low>>24, fout);
      low<<=8;
      high=(high<<8)+255;
    }
  }

  void EncodeBit1(uint p)
  {
#ifdef _WIN64
    high=low+((ulonglong(high-low)*p)>>18);
#else
    high=low+((ulonglong(high-low)*(p<<(32-18)))>>32);
#endif
    while ((low^high)<(1<<24))
    {
      putc(low>>24, fout);
      low<<=8;
      high=(high<<8)+255;
    }
  }

  void Flush()
  {
    for (int i=0; i<4; ++i)
    {
      putc(low>>24, fout);
      low<<=8;
    }
  }

  void Init()
  {
    for (int i=0; i<4; ++i)
      code=(code<<8)+getc(fin);
  }

  int DecodeBit(uint p)
  {
#ifdef _WIN64
    const uint mid=low+((ulonglong(high-low)*p)>>18);
#else
    const uint mid=low+((ulonglong(high-low)*(p<<(32-18)))>>32);
#endif
    const int bit=(code<=mid);
    if (bit)
      high=mid;
    else
      low=mid+1;

    while ((low^high)<(1<<24))
    {
      low<<=8;
      high=(high<<8)+255;
      code=(code<<8)+getc(fin);
    }

    return bit;
  }
};

template<int RATE>
struct Counter
{
  word p;

  Counter()
  {
    p=1<<15;
  }

  void UpdateBit0()
  {
    p-=p>>RATE;
  }

  void UpdateBit1()
  {
    p+=(p^65535)>>RATE;
  }
};

struct CM: Encoder
{
  Counter<2> counter0[256];
  Counter<4> counter1[256][256];
  Counter<6> counter2[2][256][17];
  int c1;
  int c2;
  int run;

  CM()
  {
    c1=0;
    c2=0;
    run=0;

    for (int i=0; i<2; ++i)
    {
      for (int j=0; j<256; ++j)
      {
        for (int k=0; k<17; ++k)
          counter2[i][j][k].p=(k<<12)-(k==16);
      }
    }
  }

  void Encode32(uint n)
  {
    for (int i=0; i<32; ++i)
    {
      if (n&(1<<31))
        Encoder::EncodeBit1(1<<17);
      else
        Encoder::EncodeBit0(1<<17);
      n+=n;
    }
  }

  uint Decode32()
  {
    uint n=0;
    for (int i=0; i<32; ++i)
      n+=n+Encoder::DecodeBit(1<<17);

    return n;
  }

  void Encode(int c)
  {
    if (c1==c2)
      ++run;
    else
      run=0;
    const int f=(run>2);

    int ctx=1;
    while (ctx<256)
    {
      const int p0=counter0[ctx].p;
      const int p1=counter1[c1][ctx].p;
      const int p2=counter1[c2][ctx].p;
      const int p=((p0+p1)*7+p2+p2)>>4;

      const int j=p>>12;
      const int x1=counter2[f][ctx][j].p;
      const int x2=counter2[f][ctx][j+1].p;
      const int ssep=x1+(((x2-x1)*(p&4095))>>12);

      const int bit=c&128;
      c+=c;

      if (bit)
      {
        Encoder::EncodeBit1(ssep*3+p);
        counter0[ctx].UpdateBit1();
        counter1[c1][ctx].UpdateBit1();
        counter2[f][ctx][j].UpdateBit1();
        counter2[f][ctx][j+1].UpdateBit1();
        ctx+=ctx+1;
      }
      else
      {
        Encoder::EncodeBit0(ssep*3+p);
        counter0[ctx].UpdateBit0();
        counter1[c1][ctx].UpdateBit0();
        counter2[f][ctx][j].UpdateBit0();
        counter2[f][ctx][j+1].UpdateBit0();
        ctx+=ctx;
      }
    }

    c2=c1;
    c1=ctx&255;
  }

  int Decode()
  {
    if (c1==c2)
      ++run;
    else
      run=0;
    const int f=(run>2);

    int ctx=1;
    while (ctx<256)
    {
      const int p0=counter0[ctx].p;
      const int p1=counter1[c1][ctx].p;
      const int p2=counter1[c2][ctx].p;
      const int p=((p0+p1)*7+p2+p2)>>4;

      const int j=p>>12;
      const int x1=counter2[f][ctx][j].p;
      const int x2=counter2[f][ctx][j+1].p;
      const int ssep=x1+(((x2-x1)*(p&4095))>>12);

      const int bit=Encoder::DecodeBit(ssep*3+p);

      if (bit)
      {
        counter0[ctx].UpdateBit1();
        counter1[c1][ctx].UpdateBit1();
        counter2[f][ctx][j].UpdateBit1();
        counter2[f][ctx][j+1].UpdateBit1();
        ctx+=ctx+1;
      }
      else
      {
        counter0[ctx].UpdateBit0();
        counter1[c1][ctx].UpdateBit0();
        counter2[f][ctx][j].UpdateBit0();
        counter2[f][ctx][j+1].UpdateBit0();
        ctx+=ctx;
      }
    }

    c2=c1;
    return c1=ctx&255;
  }
} cm;

struct CRC
{
  uint t[4][256];
  uint crc;

  CRC()
  {
    for (int i=0; i<256; ++i)
    {
      uint r=i;
      for (int j=0; j<8; ++j)
        r=(r>>1)^(-int(r&1)&0xedb88320);
      t[0][i]=r;
    }

    for (int i=0; i<256; ++i)
    {
      t[1][i]=t[0][t[0][i]&255]^(t[0][i]>>8);
      t[2][i]=t[0][t[1][i]&255]^(t[1][i]>>8);
      t[3][i]=t[0][t[2][i]&255]^(t[2][i]>>8);
    }

    crc=uint(-1);
  }

  uint operator()() const
  {
    return ~crc;
  }

  void Clear()
  {
    crc=uint(-1);
  }

  void Update(int c)
  {
    crc=t[0][(crc^c)&255]^(crc>>8);
  }

  void Update(byte* p, int n)
  {
#ifdef _WIN32
    for (; n>=4; n-=4)
    {
      crc^=*(const uint*)p;
      p+=4;
      crc=t[0][crc>>24]
          ^t[1][(crc>>16)&255]
          ^t[2][(crc>>8)&255]
          ^t[3][crc&255];
    }
#endif
    for (; n>0; --n)
      crc=t[0][(crc^*p++)&255]^(crc>>8);
  }
} crc;

void compress(int bsize)
{
  _fseeki64(fin, 0, SEEK_END);
  const long long flen=_ftelli64(fin);
  _fseeki64(fin, 0, SEEK_SET);

  if (flen>0 && bsize>flen)
    bsize=int(flen);

  byte* buf=(byte*)calloc(bsize, 5);
  if (!buf)
  {
    fprintf(stderr, "Out of memory!\n");
    exit(1);
  }

  putc(magic[0], fout);
  putc(magic[1], fout);
  putc(magic[2], fout);
  putc(magic[3], fout);

  int n;
  while ((n=fread(buf, 1, bsize, fin))>0)
  {
    crc.Update(buf, n);

    const int idx=divbwt(buf, buf, (int*)&buf[bsize], n);
    if (idx<1)
    {
      perror("Divbwt failed");
      exit(1);
    }

    cm.Encode32(n);
    cm.Encode32(idx);

    for (int i=0; i<n; ++i)
      cm.Encode(buf[i]);

    if (flen>0)
      fprintf(stderr, "%3d%%\r", int((_ftelli64(fin)*100)/flen));
  }

  free(buf);

  cm.Encode32(0); // EOF
  cm.Encode32(crc());

  cm.Flush();
}

void decompress()
{
  if (getc(fin)!=magic[0]
      ||getc(fin)!=magic[1]
      ||getc(fin)!=magic[2]
      ||getc(fin)!=magic[3])
  {
    fprintf(stderr, "Not in BCM format!\n");
    exit(1);
  }

  cm.Init();

  int bsize=0;
  byte* buf;

  int n;
  while ((n=cm.Decode32())>0)
  {
    if (!bsize)
    {
      buf=(byte*)calloc(bsize=n, 5);
      if (!buf)
      {
        fprintf(stderr, "Out of memory!\n");
        exit(1);
      }
    }

    const int idx=cm.Decode32();
    if (n>bsize || idx<1 || idx>n)
    {
      fprintf(stderr, "File corrupted!\n");
      exit(1);
    }
    // Inverse BW-transform
    int t[257]={0};
    for (int i=0; i<n; ++i)
      ++t[(buf[i]=cm.Decode())+1];
    for (int i=1; i<256; ++i)
      t[i]+=t[i-1];
    int* next=(int*)&buf[bsize];
    for (int i=0; i<n; ++i)
      next[t[buf[i]]++]=i+(i>=idx);
    for (int p=idx; p;)
    {
      p=next[p-1];
      const int c=buf[p-(p>=idx)];
      putc(c, fout);
      crc.Update(c);
    }
  }

  free(buf);

  if (cm.Decode32()!=crc())
  {
    fprintf(stderr, "CRC error!\n");
    exit(1);
  }
}

int main(int argc, char** argv)
{
  const clock_t start=clock();

  int bsize=32<<20; // 32 MB
  bool do_decomp=false;
  bool overwrite=false;

  while (argc>1 && *argv[1]=='-')
  {
    switch (argv[1][1])
    {
    case 'b':
      bsize=atoi(&argv[1][2])
          <<(argv[1][strlen(argv[1])-1]=='k'?10:20);
      if (bsize<1)
      {
        fprintf(stderr, "Block size is out of range!\n");
        exit(1);
      }
      break;
    case 'd':
      do_decomp=true;
      break;
    case 'f':
      overwrite=true;
      break;
    default:
      fprintf(stderr, "Unknown option: %s\n", argv[1]);
      exit(1);
    }

    --argc;
    ++argv;
  }

  if (argc<2)
  {
    fprintf(stderr,
        "BCM - A BWT-based file compressor, v1.22 beta\n"
        "Copyright (C) 2008-2016 Ilya Muravyov\n"
        "\n"
        "Usage: %s [options] infile [outfile]\n"
        "\n"
        "Options:\n"
        "  -b#[k] Set block size to # MB or KB (default is 32 MB)\n"
        "  -d     Decompress\n"
        "  -f     Force overwrite of output file\n", argv[0]);
    exit(1);
  }

  fin=fopen(argv[1], "rb");
  if (!fin)
  {
    perror(argv[1]);
    exit(1);
  }

  char ofname[FILENAME_MAX];
  if (argc<3)
  {
    strcpy(ofname, argv[1]);
    if (do_decomp)
    {
      const int p=strlen(ofname)-4;
      if (p>0 && !strcmp(&ofname[p], ".bcm"))
        ofname[p]='\0';
      else
        strcat(ofname, ".out");
    }
    else
      strcat(ofname, ".bcm");
  }
  else
    strcpy(ofname, argv[2]);

  if (!overwrite)
  {
    FILE* f=fopen(ofname, "rb");
    if (f)
    {
      fclose(f);

      fprintf(stderr, "%s already exists. Overwrite (y/n)? ", ofname);
      fflush(stderr);

      if (getchar()!='y')
        exit(1);
    }
  }

  fout=fopen(ofname, "wb");
  if (!fout)
  {
    perror(ofname);
    exit(1);
  }

  if (do_decomp)
  {
    fprintf(stderr, "Decompressing %s:\n", argv[1]);

    decompress();
  }
  else
  {
    fprintf(stderr, "Compressing %s:\n", argv[1]);

    compress(bsize);
  }

  fprintf(stderr, "%lld -> %lld in %1.2fs\n", _ftelli64(fin), _ftelli64(fout),
      double(clock()-start)/CLOCKS_PER_SEC);

  fclose(fin);
  fclose(fout);

#ifndef NO_UTIME
  struct _stati64 sb;
  if (_stati64(argv[1], &sb))
  {
    perror("Stat failed");
    exit(1);
  }
  struct utimbuf ub;
  ub.actime=sb.st_atime;
  ub.modtime=sb.st_mtime;
  if (utime(ofname, &ub))
  {
    perror("Utime failed");
    exit(1);
  }
#endif

  fprintf(stderr, "CRC = %08X\n", crc()); // DEBUG

  return 0;
}
