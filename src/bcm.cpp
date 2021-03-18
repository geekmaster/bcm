/*

BCM - A BWT-based file compressor

Copyright (C) 2008-2021 Ilya Muravyov

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

#include "libsais.h"

typedef unsigned char U8;
typedef unsigned short U16;
typedef unsigned int U32;
typedef unsigned long long U64;
typedef signed long long S64;

// Globals

const char magic[]="BCM!";

FILE* in;
FILE* out;

struct Encoder
{
  U32 low;
  U32 high;
  U32 code;

  Encoder()
  {
    low=0;
    high=U32(-1);
    code=0;
  }

  void Flush()
  {
    for (int i=0; i<4; ++i)
    {
      putc(low>>24, out);
      low<<=8;
    }
  }

  void Init()
  {
    for (int i=0; i<4; ++i)
      code=(code<<8)+getc(in);
  }

  template<int P_LOG>
  void EncodeBit(int bit, U32 p)
  {
    const U32 mid=low+((U64(high-low)*p)>>P_LOG);

    if (bit)
      high=mid;
    else
      low=mid+1;

    // Renormalize
    while ((low^high)<(1<<24))
    {
      putc(low>>24, out);
      low<<=8;
      high=(high<<8)+255;
    }
  }

  template<int P_LOG>
  int DecodeBit(U32 p)
  {
    const U32 mid=low+((U64(high-low)*p)>>P_LOG);

    const int bit=(code<=mid);
    if (bit)
      high=mid;
    else
      low=mid+1;

    // Renormalize
    while ((low^high)<(1<<24))
    {
      low<<=8;
      high=(high<<8)+255;
      code=(code<<8)+getc(in);
    }

    return bit;
  }
};

template<int RATE>
struct Counter
{
  U16 p;

  Counter()
  {
    p=1<<15; // 0.5
  }

  void Update1()
  {
    p+=(p^0xFFFF)>>RATE;
  }

  void Update0()
  {
    p-=p>>RATE;
  }
};

struct CM: Encoder
{
  Counter<2> counter0[256];
  Counter<4> counter1[256][256];
  Counter<6> counter2[2][256][17];
  int run;
  int c1;
  int c2;

  CM()
  {
    run=0;
    c1=0;
    c2=0;

    for (int i=0; i<2; ++i)
    {
      for (int j=0; j<256; ++j)
      {
        for (int k=0; k<=16; ++k)
          counter2[i][j][k].p=(k<<12)-(k==16);
      }
    }
  }

  void Put32(U32 x)
  {
    for (U32 i=1<<31; i>0; i>>=1)
      EncodeBit<1>(x&i, 1); // p=0.5
  }

  U32 Get32()
  {
    U32 x=0;
    for (int i=0; i<32; ++i)
      x+=x+DecodeBit<1>(1); // p=0.5

    return x;
  }

  void Put(int c)
  {
    const int f=(run>2);

    int ctx=1;
    for (int i=128; i>0; i>>=1)
    {
      const int p0=counter0[ctx].p;
      const int p1=counter1[c1][ctx].p;
      const int p2=counter1[c2][ctx].p;
      const int p=(((p0+p1)*7)+p2+p2)>>4;

      // SSE with linear interpolation
      const int j=p>>12;
      const int x1=counter2[f][ctx][j].p;
      const int x2=counter2[f][ctx][j+1].p;
      const int ssep=x1+(((x2-x1)*(p&4095))>>12);

      if (c&i)
      {
        EncodeBit<18>(1, p+ssep+ssep+ssep);

        counter0[ctx].Update1();
        counter1[c1][ctx].Update1();
        counter2[f][ctx][j].Update1();
        counter2[f][ctx][j+1].Update1();

        ctx+=ctx+1;
      }
      else
      {
        EncodeBit<18>(0, p+ssep+ssep+ssep);

        counter0[ctx].Update0();
        counter1[c1][ctx].Update0();
        counter2[f][ctx][j].Update0();
        counter2[f][ctx][j+1].Update0();

        ctx+=ctx;
      }
    }

    c2=c1;
    c1=ctx-256;

    if (c1==c2)
      ++run;
    else
      run=0;
  }

  int Get()
  {
    const int f=(run>2);

    int ctx=1;
    while (ctx<256)
    {
      const int p0=counter0[ctx].p;
      const int p1=counter1[c1][ctx].p;
      const int p2=counter1[c2][ctx].p;
      const int p=(((p0+p1)*7)+p2+p2)>>4;

      // SSE with linear interpolation
      const int j=p>>12;
      const int x1=counter2[f][ctx][j].p;
      const int x2=counter2[f][ctx][j+1].p;
      const int ssep=x1+(((x2-x1)*(p&4095))>>12);

      if (DecodeBit<18>(p+ssep+ssep+ssep))
      {
        counter0[ctx].Update1();
        counter1[c1][ctx].Update1();
        counter2[f][ctx][j].Update1();
        counter2[f][ctx][j+1].Update1();

        ctx+=ctx+1;
      }
      else
      {
        counter0[ctx].Update0();
        counter1[c1][ctx].Update0();
        counter2[f][ctx][j].Update0();
        counter2[f][ctx][j+1].Update0();

        ctx+=ctx;
      }
    }

    c2=c1;
    c1=ctx-256;

    if (c1==c2)
      ++run;
    else
      run=0;

    return c1;
  }
} cm;

struct CRC
{
  U32 tab[256];
  U32 crc;

  CRC()
  {
    for (int i=0; i<256; ++i)
    {
      U32 r=i;
      for (int j=0; j<8; ++j)
        r=(r>>1)^(0xEDB88320&-int(r&1));
      tab[i]=r;
    }
    crc=U32(-1);
  }

  U32 operator()() const
  {
    return crc^U32(-1);
  }

  void Update(int c)
  {
    crc=(crc>>8)^tab[(crc^c)&255];
  }

  void Update(U8* buf, int n)
  {
    for (int i=0; i<n; ++i)
      crc=(crc>>8)^tab[(crc^buf[i])&255];
  }
} crc;

template<typename T>
inline T* MemAlloc(size_t n)
{
  T* p=(T*)malloc(n*sizeof(T));
  if (!p)
  {
    perror("Malloc() failed");
    exit(1);
  }
  return p;
}

void Compress(int level)
{
  const int tab[10]=
  {
    0,
    1<<20,      // -1 - 1 MB
    1<<22,      // -2 - 4 MB
    1<<23,      // -3 - 8 MB
    0x00FFFFFF, // -4 - ~16 MB (Default)
    1<<25,      // -5 - 32 MB
    1<<26,      // -6 - 64 MB
    1<<27,      // -7 - 128 MB
    1<<28,      // -8 - 256 MB
    0x7FFFFFFF, // -9 - ~2 GB
  };
  int bsize=tab[level]; // Block size

  if (_fseeki64(in, 0, SEEK_END))
  {
    perror("Fseek() failed");
    exit(1);
  }
  const S64 flen=_ftelli64(in);
  if (flen<0)
  {
    perror("Ftell() failed");
    exit(1);
  }
  rewind(in);

  if (bsize>flen)
    bsize=int(flen);

  U8* buf=MemAlloc<U8>(bsize);
  int* ptr=MemAlloc<int>(bsize);

  int n;
  while ((n=fread(buf, 1, bsize, in))>0)
  {
    crc.Update(buf, n);

    const int idx=libsais_bwt(buf, buf, ptr, n);
    if (idx<1)
    {
      fprintf(stderr, "BWT() failed: idx = %d\n", idx);
      exit(1);
    }

    cm.Put32(n); // Block size
    cm.Put32(idx); // BWT index

    for (int i=0; i<n; ++i)
      cm.Put(buf[i]);

    fprintf(stderr, "%lld -> %lld\r", _ftelli64(in), _ftelli64(out));
  }

  cm.Put32(0); // EOF
  cm.Put32(crc()); // CRC32

  cm.Flush();

  free(buf);
  free(ptr);
}

void Decompress()
{
  int cnt[257];

  int bsize=0;
  U8* buf=nullptr;
  U32* ptr=nullptr;

  cm.Init();

  int n;
  while ((n=cm.Get32())>0)
  {
    if (!bsize)
    {
      if ((bsize=n)>=(1<<24)) // 5*N
        buf=MemAlloc<U8>(bsize);
      ptr=MemAlloc<U32>(bsize);
    }

    const int idx=cm.Get32();
    if (n>bsize || idx<1 || idx>n)
    {
      fprintf(stderr, "Corrupt input!\n");
      exit(1);
    }

    // Inverse BW-transform

    if (n>=(1<<24)) // 5*N
    {
      memset(cnt, 0, sizeof(cnt));
      for (int i=0; i<n; ++i)
        ++cnt[(buf[i]=cm.Get())+1];
      for (int i=1; i<256; ++i)
        cnt[i]+=cnt[i-1];

      for (int i=0; i<idx; ++i)
        ptr[cnt[buf[i]]++]=i;
      for (int i=idx+1; i<=n; ++i)
        ptr[cnt[buf[i-1]]++]=i;

      int p=idx;
      for (int i=0; i<n; ++i)
      {
        p=ptr[p-1];
        const int c=buf[p-(p>=idx)];
        crc.Update(c);
        putc(c, out);
      }
    }
    else // 4*N
    {
      memset(cnt, 0, sizeof(cnt));
      for (int i=0; i<n; ++i)
        ++cnt[(ptr[i]=cm.Get())+1];
      for (int i=1; i<256; ++i)
        cnt[i]+=cnt[i-1];

      for (int i=0; i<idx; ++i)
        ptr[cnt[ptr[i]&255]++]|=i<<8;
      for (int i=idx+1; i<=n; ++i)
        ptr[cnt[ptr[i-1]&255]++]|=i<<8;

      int p=idx;
      for (int i=0; i<n; ++i)
      {
        p=ptr[p-1]>>8;
        const int c=ptr[p-(p>=idx)];
        crc.Update(c);
        putc(c, out);
      }
    }

    fprintf(stderr, "%lld -> %lld\r", _ftelli64(in), _ftelli64(out));
  }

  if (cm.Get32()!=crc())
  {
    fprintf(stderr, "CRC error!\n");
    exit(1);
  }

  if (buf)
    free(buf);
  free(ptr);
}

int main(int argc, char** argv)
{
  const clock_t start=clock();

  int level=4;
  int decompress=0;
  int overwrite=0;

  while (argc>1 && *argv[1]=='-')
  {
    for (int i=1; argv[1][i]!='\0'; ++i)
    {
      switch (argv[1][i])
      {
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
        level=argv[1][i]-'0';
        break;
      case 'd':
        decompress=1;
        break;
      case 'f':
        overwrite=1;
        break;
      default:
        fprintf(stderr, "Unknown option '-%c'\n", argv[1][i]);
        exit(1);
      }
    }

    --argc;
    ++argv;
  }

  if (argc<2)
  {
    fprintf(stderr,
        "BCM - A BWT-based file compressor, v1.60\n"
        "Copyright (C) 2008-2021 Ilya Muravyov\n"
        "\n"
        "Usage: BCM [options] infile [outfile]\n"
        "\n"
        "Options:\n"
        "  -1 .. -9 Set block size to 1 MB .. 2 GB\n"
        "  -d       Decompress\n"
        "  -f       Force overwrite of output file\n");
    exit(1);
  }

  in=fopen(argv[1], "rb");
  if (!in)
  {
    perror(argv[1]);
    exit(1);
  }

  char ofname[FILENAME_MAX];
  if (argc<3)
  {
    strcpy(ofname, argv[1]);
    if (decompress)
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

      fprintf(stderr, "File '%s' already exists. Overwrite (y/n)? ", ofname);
      fflush(stderr);

      if (getchar()!='y')
      {
        fprintf(stderr, "Not overwritten\n");
        exit(1);
      }
    }
  }

  if (decompress)
  {
    if (getc(in)!=magic[0]
        || getc(in)!=magic[1]
        || getc(in)!=magic[2]
        || getc(in)!=magic[3])
    {
      fprintf(stderr, "%s: Not in BCM format\n", argv[1]);
      exit(1);
    }

    out=fopen(ofname, "wb");
    if (!out)
    {
      perror(ofname);
      exit(1);
    }

    fprintf(stderr, "Decompressing '%s':\n", argv[1]);

    Decompress();
  }
  else
  {
    out=fopen(ofname, "wb");
    if (!out)
    {
      perror(ofname);
      exit(1);
    }

    putc(magic[0], out);
    putc(magic[1], out);
    putc(magic[2], out);
    putc(magic[3], out);

    fprintf(stderr, "Compressing '%s':\n", argv[1]);

    Compress(level);
  }

  fprintf(stderr, "%lld -> %lld in %1.1f sec\n",
      _ftelli64(in), _ftelli64(out), double(clock()-start)/CLOCKS_PER_SEC);

  fclose(in);
  fclose(out);

#ifndef NO_UTIME
  struct _stati64 sb;
  if (_stati64(argv[1], &sb))
  {
    perror("Stat() failed");
    exit(1);
  }
  struct utimbuf ub;
  ub.actime=sb.st_atime;
  ub.modtime=sb.st_mtime;
  if (utime(ofname, &ub))
  {
    perror("Utime() failed");
    exit(1);
  }
#endif

  return 0;
}
