/* Author: Wang Yi <godspeed_china@yeah.net>
   chopped down and converted to older C standard for xorgxrdp
*/
#ifndef wyhash_final_version
#define wyhash_final_version
#ifndef WYHASH_CONDOM
#define WYHASH_CONDOM 0
#endif
#include <stdint.h>
#include <string.h>
#if defined(_MSC_VER) && defined(_M_X64)
  #include <intrin.h>
  #pragma intrinsic(_umul128)
#endif
#if defined(__GNUC__) || defined(__INTEL_COMPILER) || defined(__clang__)
  #define _likely_(x) __builtin_expect(x,1)
  #define _unlikely_(x) __builtin_expect(x,0)
#else
  #define _likely_(x) (x)
  #define _unlikely_(x) (x)
#endif
static __inline__ uint64_t _wyrot(uint64_t x) { return (x>>32)|(x<<32); }
static __inline__ void _wymum(uint64_t *A, uint64_t *B){
#if defined(__SIZEOF_INT128__)
  __uint128_t r;
  r=*A; r*=*B; 
  #if(WYHASH_CONDOM>1)
  *A^=(uint64_t)r; *B^=(uint64_t)(r>>64);
  #else
  *A=(uint64_t)r; *B=(uint64_t)(r>>64);
  #endif
#elif defined(_MSC_VER) && defined(_M_X64)
  #if(WYHASH_CONDOM>1)
  uint64_t  a,  b;
  a=_umul128(*A,*B,&b);
  *A^=a;  *B^=b;
  #else
  *A=_umul128(*A,*B,B);
  #endif
#else
  uint64_t ha, hb, la, lb, hi, lo;
  uint64_t rh, rm0, rm1, rl, t, c;
  ha=*A>>32; hb=*B>>32; la=(uint32_t)*A; lb=(uint32_t)*B;
  rh=ha*hb; rm0=ha*lb; rm1=hb*la; rl=la*lb; t=rl+(rm0<<32); c=t<rl;
  lo=t+(rm1<<32); c+=lo<t; hi=rh+(rm0>>32)+(rm1>>32)+c;
  #if(WYHASH_CONDOM>1)
  *A^=lo;  *B^=hi;
  #else
  *A=lo;  *B=hi;
  #endif
#endif
}
static __inline__ uint64_t _wymix(uint64_t A, uint64_t B){ _wymum(&A,&B); return A^B; }
#ifndef WYHASH_LITTLE_ENDIAN
  #if defined(_WIN32) || defined(__LITTLE_ENDIAN__) || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
    #define WYHASH_LITTLE_ENDIAN 1
  #elif defined(__BIG_ENDIAN__) || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
    #define WYHASH_LITTLE_ENDIAN 0
  #endif
#endif
#if (WYHASH_LITTLE_ENDIAN)
static __inline__ uint64_t _wyr8(const uint8_t *p) { uint64_t v; memcpy(&v, p, 8); return v;}
static __inline__ uint64_t _wyr4(const uint8_t *p) { unsigned v; memcpy(&v, p, 4); return v;}
#elif defined(__GNUC__) || defined(__INTEL_COMPILER) || defined(__clang__)
static __inline__ uint64_t _wyr8(const uint8_t *p) { uint64_t v; memcpy(&v, p, 8); return __builtin_bswap64(v);}
static __inline__ uint64_t _wyr4(const uint8_t *p) { unsigned v; memcpy(&v, p, 4); return __builtin_bswap32(v);}
#elif defined(_MSC_VER)
static __inline__ uint64_t _wyr8(const uint8_t *p) { uint64_t v; memcpy(&v, p, 8); return _byteswap_uint64(v);}
static __inline__ uint64_t _wyr4(const uint8_t *p) { unsigned v; memcpy(&v, p, 4); return _byteswap_ulong(v);}
#endif
static __inline__ uint64_t _wyr3(const uint8_t *p, unsigned k) { return (((uint64_t)p[0])<<16)|(((uint64_t)p[k>>1])<<8)|p[k-1];}
static __inline__ uint64_t _wyfinish16(const uint8_t *p, uint64_t len, uint64_t seed, const uint64_t *secret, uint64_t i){
#if(WYHASH_CONDOM>0)
  uint64_t a, b;
  if(_likely_(i<=8)){
    if(_likely_(i>=4)){ a=_wyr4(p); b=_wyr4(p+i-4); }
    else if (_likely_(i)){ a=_wyr3(p,i); b=0; }
    else a=b=0;
  } 
  else{ a=_wyr8(p); b=_wyr8(p+i-8); }
  return _wymix(secret[1]^len,_wymix(a^secret[1], b^seed));
#else
  #define oneshot_shift ((i<8)*((8-i)<<3))
  return _wymix(secret[1]^len,_wymix((_wyr8(p)<<oneshot_shift)^secret[1],(_wyr8(p+i-8)>>oneshot_shift)^seed));
#endif
}

static __inline__ uint64_t _wyfinish(const uint8_t *p, uint64_t len, uint64_t seed, const uint64_t *secret, uint64_t i){
  if(_likely_(i<=16)) return _wyfinish16(p,len,seed,secret,i);
  return _wyfinish(p+16,len,_wymix(_wyr8(p)^secret[1],_wyr8(p+8)^seed),secret,i-16);
}

static __inline__ uint64_t wyhash(const void *key, uint64_t len, uint64_t seed, const uint64_t *secret){
  const uint8_t *p;
  uint64_t i;
  uint64_t see1;
  p=(const uint8_t *)key;
  i=len; seed^=*secret;
  if(_unlikely_(i>64)){
    see1=seed;
    do{
      seed=_wymix(_wyr8(p)^secret[1],_wyr8(p+8)^seed)^_wymix(_wyr8(p+16)^secret[2],_wyr8(p+24)^seed);
      see1=_wymix(_wyr8(p+32)^secret[3],_wyr8(p+40)^see1)^_wymix(_wyr8(p+48)^secret[4],_wyr8(p+56)^see1);
      p+=64; i-=64;
    }while(i>64);
    seed^=see1;
  }
  return _wyfinish(p,len,seed,secret,i);
}
const uint64_t _wyp[5] = {0xa0761d6478bd642full, 0xe7037ed1a0b428dbull, 0x8ebc6af09c88c6e3ull, 0x589965cc75374cc3ull, 0x1d8e4e27c47d124full};
static __inline__ uint64_t wyhash64(uint64_t A, uint64_t B){  A^=_wyp[0]; B^=_wyp[1];  _wymum(&A,&B);  return _wymix(A^_wyp[0],B^_wyp[1]);}
static __inline__ uint64_t wyrand(uint64_t *seed){  *seed+=_wyp[0]; return _wymix(*seed,*seed^_wyp[1]);}
#endif
