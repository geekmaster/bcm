/*

BCM - A BWT-based file compressor

Written and placed in the public domain by Ilya Muravyov

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

typedef unsigned char U8;
typedef unsigned short U16;
typedef unsigned int U32;
typedef unsigned long long U64;
typedef signed long long S64;

// Globals

FILE* g_in;
FILE* g_out;

const char g_magic[]="BCM!";

struct Encoder
{
  U32 low;
  U32 high;
  U32 code;

  Encoder()
  {
    low=0;
    high=0xFFFFFFFF;
    code=0;
  }

  void Flush()
  {
    for (int i=0; i<4; ++i)
    {
      putc(low>>24, g_out);
      low<<=8;
    }
  }

  void Init()
  {
    for (int i=0; i<4; ++i)
      code=(code<<8)+getc(g_in);
  }

  template<int N>
  void EncodeDirectBits(U32 x)
  {
    for (U32 i=1<<(N-1); i!=0; i>>=1)
    {
      if (x&i)
        high=low+((high-low)>>1);
      else
        low+=((high-low)>>1)+1;

      if ((low^high)<(1<<24))
      {
        putc(low>>24, g_out);
        low<<=8;
        high=(high<<8)+255;
      }
    }
  }

  void EncodeBit1(U32 p)
  {
#ifdef _WIN64
    high=low+((U64(high-low)*p)>>18);
#else
    high=low+((U64(high-low)*(p<<(32-18)))>>32);
#endif
    while ((low^high)<(1<<24))
    {
      putc(low>>24, g_out);
      low<<=8;
      high=(high<<8)+255;
    }
  }

  void EncodeBit0(U32 p)
  {
#ifdef _WIN64
    low+=((U64(high-low)*p)>>18)+1;
#else
    low+=((U64(high-low)*(p<<(32-18)))>>32)+1;
#endif
    while ((low^high)<(1<<24))
    {
      putc(low>>24, g_out);
      low<<=8;
      high=(high<<8)+255;
    }
  }

  template<int N>
  U32 DecodeDirectBits()
  {
    U32 x=0;

    for (int i=0; i<N; ++i)
    {
      const U32 mid=low+((high-low)>>1);
      if (code<=mid)
      {
        high=mid;
        x+=x+1;
      }
      else
      {
        low=mid+1;
        x+=x;
      }

      if ((low^high)<(1<<24))
      {
        low<<=8;
        high=(high<<8)+255;
        code=(code<<8)+getc(g_in);
      }
    }

    return x;
  }

  int DecodeBit(U32 p)
  {
#ifdef _WIN64
    const U32 mid=low+((U64(high-low)*p)>>18);
#else
    const U32 mid=low+((U64(high-low)*(p<<(32-18)))>>32);
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
      code=(code<<8)+getc(g_in);
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

  void UpdateBit0()
  {
    p-=p>>RATE;
  }

  void UpdateBit1()
  {
    p+=(p^0xFFFF)>>RATE;
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
      const int p=(((p0+p1)*7)+p2+p2)>>4;

      const int j=p>>12;
      const int x1=counter2[f][ctx][j].p;
      const int x2=counter2[f][ctx][j+1].p;
      const int ssep=x1+(((x2-x1)*(p&4095))>>12);

      if (c&128)
      {
        Encoder::EncodeBit1((ssep*3)+p);
        counter0[ctx].UpdateBit1();
        counter1[c1][ctx].UpdateBit1();
        counter2[f][ctx][j].UpdateBit1();
        counter2[f][ctx][j+1].UpdateBit1();
        ctx+=ctx+1;
      }
      else
      {
        Encoder::EncodeBit0((ssep*3)+p);
        counter0[ctx].UpdateBit0();
        counter1[c1][ctx].UpdateBit0();
        counter2[f][ctx][j].UpdateBit0();
        counter2[f][ctx][j+1].UpdateBit0();
        ctx+=ctx;
      }

      c+=c;
    }

    c2=c1;
    c1=ctx-256;
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
      const int p=(((p0+p1)*7)+p2+p2)>>4;

      const int j=p>>12;
      const int x1=counter2[f][ctx][j].p;
      const int x2=counter2[f][ctx][j+1].p;
      const int ssep=x1+(((x2-x1)*(p&4095))>>12);

      if (Encoder::DecodeBit((ssep*3)+p))
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
    return c1=ctx-256;
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

    crc=0xFFFFFFFF;
  }

  void Clear()
  {
    crc=0xFFFFFFFF;
  }

  U32 operator()() const
  {
    return crc^0xFFFFFFFF;
  }

  void Update(int c)
  {
    crc=(crc>>8)^tab[(crc^c)&255];
  }
} crc;

template<typename T>
inline T* mem_alloc(size_t n)
{
  T* p=(T*)malloc(n*sizeof(T));
  if (!p)
  {
    perror("Malloc() failed");
    exit(1);
  }

  return p;
}

#define mem_free(p) free(p)

void compress(int level)
{
  const int config_tab[10]=
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

  int block_size=config_tab[level];

  _fseeki64(g_in, 0, SEEK_END);
  const S64 file_size=_ftelli64(g_in);
  _fseeki64(g_in, 0, SEEK_SET);

  if (file_size>0 && block_size>file_size)
    block_size=int(file_size);

  U8* buf=mem_alloc<U8>(block_size);
  int* ptr=mem_alloc<int>(block_size);

  int n;
  while ((n=fread(buf, 1, block_size, g_in))>0)
  {
    for (int i=0; i<n; ++i)
      crc.Update(buf[i]);

    const int idx=divbwt(buf, buf, ptr, n);
    if (idx<1)
    {
      perror("Divbwt() failed");
      exit(1);
    }

    cm.EncodeDirectBits<32>(n);
    cm.EncodeDirectBits<32>(idx);

    for (int i=0; i<n; ++i)
      cm.Encode(buf[i]);

    fprintf(stderr, "%lld -> %lld\r", _ftelli64(g_in), _ftelli64(g_out));
  }

  cm.EncodeDirectBits<32>(0); // EOF
  cm.EncodeDirectBits<32>(crc());

  cm.Flush();

  mem_free(buf);
  mem_free(ptr);
}

void decompress()
{
  cm.Init();

  int block_size=0;
  U8* buf=NULL;
  U32* ptr=NULL;

  int n;
  while ((n=cm.DecodeDirectBits<32>())>0)
  {
    if (block_size==0)
    {
      if ((block_size=n)>=(1<<24)) // 5*N
        buf=mem_alloc<U8>(block_size);

      ptr=mem_alloc<U32>(block_size);
    }

    const int idx=cm.DecodeDirectBits<32>();
    if (n>block_size || idx<1 || idx>n)
    {
      fprintf(stderr, "Corrupt input!\n");
      exit(1);
    }

    // Inverse BW-transform
    if (n>=(1<<24)) // 5*N
    {
      int t[257]={0};
      for (int i=0; i<n; ++i)
        ++t[(buf[i]=cm.Decode())+1];
      for (int i=1; i<256; ++i)
        t[i]+=t[i-1];
      for (int i=0; i<n; ++i)
        ptr[t[buf[i]]++]=i+(i>=idx);
      for (int p=idx; p;)
      {
        p=ptr[p-1];
        const int c=buf[p-(p>=idx)];
        crc.Update(c);
        putc(c, g_out);
      }
    }
    else // 4*N
    {
      int t[257]={0};
      for (int i=0; i<n; ++i)
        ++t[(ptr[i]=cm.Decode())+1];
      for (int i=1; i<256; ++i)
        t[i]+=t[i-1];
      for (int i=0; i<n; ++i)
        ptr[t[ptr[i]&255]++]|=(i+(i>=idx))<<8;
      for (int p=idx; p;)
      {
        p=ptr[p-1]>>8;
        const int c=ptr[p-(p>=idx)]&255;
        crc.Update(c);
        putc(c, g_out);
      }
    }

    fprintf(stderr, "%lld -> %lld\r", _ftelli64(g_in), _ftelli64(g_out));
  }

  if (cm.DecodeDirectBits<32>()!=crc())
  {
    fprintf(stderr, "CRC error!\n");
    exit(1);
  }

  mem_free(buf);
  mem_free(ptr);
}

int main(int argc, char** argv)
{
  const clock_t start=clock();

  int level=4;
  bool do_decomp=false;
  bool overwrite=false;

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
#ifdef _WIN64
      case '9':
#endif
        level=argv[1][i]-'0';
        break;
      case 'd':
        do_decomp=true;
        break;
      case 'f':
        overwrite=true;
        break;
      default:
        fprintf(stderr, "Unknown option: -%c\n", argv[1][i]);
        exit(1);
      }
    }

    --argc;
    ++argv;
  }

  if (argc<2)
  {
    fprintf(stderr,
        "BCM - A BWT-based file compressor, v1.40\n"
        "\n"
        "Usage: BCM [options] infile [outfile]\n"
        "\n"
        "Options:\n"
#ifdef _WIN64
        "  -1 .. -9 Set block size to 1 MB .. 2 GB\n"
#else
        "  -1 .. -8 Set block size to 1 MB .. 256 MB\n"
#endif
        "  -d       Decompress\n"
        "  -f       Force overwrite of output file\n");
    exit(1);
  }

  g_in=fopen(argv[1], "rb");
  if (!g_in)
  {
    perror(argv[1]);
    exit(1);
  }

  char out_name[FILENAME_MAX];
  if (argc<3)
  {
    strcpy(out_name, argv[1]);
    if (do_decomp)
    {
      const int p=strlen(out_name)-4;
      if (p>0 && strcmp(&out_name[p], ".bcm")==0)
        out_name[p]='\0';
      else
        strcat(out_name, ".out");
    }
    else
      strcat(out_name, ".bcm");
  }
  else
    strcpy(out_name, argv[2]);

  if (!overwrite)
  {
    FILE* f=fopen(out_name, "rb");
    if (f)
    {
      fclose(f);

      fprintf(stderr, "%s already exists. Overwrite (y/n)? ", out_name);
      fflush(stderr);

      if (getchar()!='y')
      {
        fprintf(stderr, "Not overwritten\n");
        exit(1);
      }
    }
  }

  if (do_decomp)
  {
    if (getc(g_in)!=g_magic[0]
        ||getc(g_in)!=g_magic[1]
        ||getc(g_in)!=g_magic[2]
        ||getc(g_in)!=g_magic[3])
    {
      fprintf(stderr, "%s: Not in BCM format\n", argv[1]);
      exit(1);
    }

    g_out=fopen(out_name, "wb");
    if (!g_out)
    {
      perror(out_name);
      exit(1);
    }

    fprintf(stderr, "Decompressing %s:\n", argv[1]);

    decompress();
  }
  else
  {
    g_out=fopen(out_name, "wb");
    if (!g_out)
    {
      perror(out_name);
      exit(1);
    }

    putc(g_magic[0], g_out);
    putc(g_magic[1], g_out);
    putc(g_magic[2], g_out);
    putc(g_magic[3], g_out);

    fprintf(stderr, "Compressing %s:\n", argv[1]);

    compress(level);
  }

  fprintf(stderr, "%lld -> %lld in %1.1f sec\n", _ftelli64(g_in),
      _ftelli64(g_out), double(clock()-start)/CLOCKS_PER_SEC);

  fclose(g_in);
  fclose(g_out);

#ifndef NO_UTIME
  struct _stati64 sb;
  if (_stati64(argv[1], &sb)!=0)
  {
    perror("Stat() failed");
    exit(1);
  }
  struct utimbuf ub;
  ub.actime=sb.st_atime;
  ub.modtime=sb.st_mtime;
  if (utime(out_name, &ub)!=0)
  {
    perror("Utime() failed");
    exit(1);
  }
#endif

  return 0;
}
