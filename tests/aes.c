/*
 * Copyright © 2022 - polfosol
 * μAES ™ is a minimalist all-in-one library for AES encryption
 */

/*
 * You can use different AES algorithms by changing this macro.
 * Default is AES-128
 */
#define AES___ 128 /* or 256 (or 192; not standardized in some modes) */

/*
 * AES block-cipher modes of operation. The following modes can be enabled /
 * disabled by setting their corresponding macros to TRUE (1) or FALSE (0).
 */
#define BLOCKCIPHERS 1
#define AEAD_MODES 1 /* authenticated encryption with associated data.  */

#if BLOCKCIPHERS
#define ECB 1 /* electronic code-book (NIST SP 800-38A)          */
#define CBC 1 /* cipher block chaining (NIST SP 800-38A)         */
#define CFB 1 /* cipher feedback (NIST SP 800-38A)               */
#define OFB 1 /* output feedback (NIST SP 800-38A)               */
#define CTR 1 /* counter-block (NIST SP 800-38A)                 */
#define XEX 1 /* xor-encrypt-xor (NIST SP 800-38E)               */
#define KWA 1 /* key wrap with authentication (NIST SP 800-38F)  */
#define FPE 1 /* format-preserving encryption (NIST SP 800-38G)  */
#endif

#if AEAD_MODES
#define CMAC 1 /* message authentication code (NIST SP 800-38B)   */

#if CTR
#define CCM 1     /* counter with CBC-MAC (RFC-3610/NIST SP 800-38C) */
#define GCM 1     /* Galois/counter mode with GMAC (NIST SP 800-38D) */
#define EAX 1     /* encrypt-authenticate-translate (ANSI C12.22)    */
#define SIV 1     /* synthetic initialization vector (RFC-5297)      */
#define GCM_SIV 1 /* nonce misuse-resistant AES-GCM (RFC-8452)       */
#endif

#if XEX
#define OCB 1 /* offset codebook mode with PMAC (RFC-7253)       */
#endif

#define POLY1305 1 /* poly1305-AES mac (https://cr.yp.to/mac.html)    */
#endif

#if CBC
#define CTS 1 /* ciphertext stealing (CS3: unconditional swap)   */
#endif

#if XEX
#define XTS 1 /* XEX tweaked-codebook with ciphertext stealing   */
#endif

#if CTR
#define CTR_NA 1 /* pure counter mode, with no authentication       */
#endif

#if EAX
#define EAXP 1 /* EAX-prime, as specified by IEEE Std 1703        */
#endif

#define WTF !(AEAD_MODES || BLOCKCIPHERS)
#define M_RIJNDAEL WTF /* none of above; just rijndael API. dude.., why?  */

/**----------------------------------------------------------------------------
Refer to the BOTTOM OF THIS DOCUMENT for some explanations about these macros:
 -----------------------------------------------------------------------------*/

#if ECB || (CBC && !CTS) || (XEX && !XTS)
#define AES_PADDING 0 /* standard values: (1) PKCS#7  (2) ISO/IEC7816-4  */
#endif

#if ECB || CBC || XEX || KWA || M_RIJNDAEL
#define DECRYPTION 1 /* rijndael decryption is NOT required otherwise.  */
#endif

#if FPE
#define CUSTOM_ALPHABET                                           \
    0          /* if disabled, use default alphabet (digits 0..9) \
                */
#define FF_X 1 /* algorithm type:  (1) for FF1, or (3) for FF3-1  */
#endif

#if CTR_NA
#define CTR_IV_LENGTH 12 /* for using the last 32 bits as counter           */
#define CTR_STARTVALUE 1 /* recommended value according to the RFC-3686.    */
#endif

#if CCM
#define CCM_NONCE_LEN 11 /* for 32-bit count (since one byte is reserved).  */
#define CCM_TAG_LEN 16   /* must be an even number in the range of 4..16    */
#endif

#if GCM
#define GCM_NONCE_LEN 12 /* RECOMMENDED. but other values are supported.    */
#endif

#if EAX && !EAXP
#define EAX_NONCE_LEN 16 /* no specified limit; can be arbitrarily large.   */
#endif

#if OCB
#define OCB_NONCE_LEN 12 /* RECOMMENDED. must be positive and less than 16. */
#define OCB_TAG_LEN 16   /* again, please see the bottom of this document!  */
#endif

/**----------------------------------------------------------------------------
Since <stdint.h> is not a part of ANSI-C, we may need a 'trick' to use uint8_t
 -----------------------------------------------------------------------------*/
#include <string.h>
#if __STDC_VERSION__ > 199900L || __cplusplus > 201100L || defined(_MSC_VER)
#include <stdint.h>
#else
#include <limits.h>
#if CHAR_BIT == 8
typedef unsigned char uint8_t;
#endif
#if INT_MAX > 200000L
typedef int int32_t;
#else
typedef long int32_t;
#endif
#endif

/**----------------------------------------------------------------------------
Encryption/decryption of a single block with Rijndael
 -----------------------------------------------------------------------------*/
#if M_RIJNDAEL
void AES_Cipher(const uint8_t *key,  /* encryption/decryption key    */
                const char mode,     /* encrypt: 'E', decrypt: 'D'   */
                const uint8_t x[16], /* input bytes (or input block) */
                uint8_t y[16]);      /* output block                 */
#endif

/**----------------------------------------------------------------------------
Main functions for ECB-AES block ciphering
 -----------------------------------------------------------------------------*/
#if ECB
void AES_ECB_encrypt(const uint8_t *key,    /* encryption key               */
                     const uint8_t *pntxt,  /* plaintext buffer             */
                     const size_t ptextLen, /* length of input plain text   */
                     uint8_t *crtxt);       /* cipher-text result           */

char AES_ECB_decrypt(const uint8_t *key,    /* decryption key               */
                     const uint8_t *crtxt,  /* cipher-text buffer           */
                     const size_t crtxtLen, /* length of input cipher text  */
                     uint8_t *pntxt);       /* plaintext result             */
#endif                                      /* ECB */

/**----------------------------------------------------------------------------
Main functions for CBC-AES block ciphering
 -----------------------------------------------------------------------------*/
#if CBC
char AES_CBC_encrypt(const uint8_t *key,     /* encryption key               */
                     const uint8_t iVec[16], /* initialization vector        */
                     const uint8_t *pntxt,   /* plaintext buffer             */
                     const size_t ptextLen,  /* length of input plain text   */
                     uint8_t *crtxt);        /* cipher-text result           */

char AES_CBC_decrypt(const uint8_t *key,     /* decryption key               */
                     const uint8_t iVec[16], /* initialization vector        */
                     const uint8_t *crtxt,   /* cipher-text buffer           */
                     const size_t crtxtLen,  /* length of input cipher text  */
                     uint8_t *pntxt);        /* plaintext result             */
#endif                                       /* CBC */

/**----------------------------------------------------------------------------
Main functions for CFB-AES block ciphering
 -----------------------------------------------------------------------------*/
#if CFB
void AES_CFB_encrypt(const uint8_t *key,     /* encryption key               */
                     const uint8_t iVec[16], /* initialization vector        */
                     const uint8_t *pntxt,   /* plaintext buffer             */
                     const size_t ptextLen,  /* length of input plain text   */
                     uint8_t *crtxt);        /* cipher-text result           */

void AES_CFB_decrypt(const uint8_t *key,     /* decryption key               */
                     const uint8_t iVec[16], /* initialization vector        */
                     const uint8_t *crtxt,   /* cipher-text buffer           */
                     const size_t crtxtLen,  /* length of input cipher text  */
                     uint8_t *pntxt);        /* plaintext result             */
#endif                                       /* CFB */

/**----------------------------------------------------------------------------
Main functions for OFB-AES block ciphering
 -----------------------------------------------------------------------------*/
#if OFB
void AES_OFB_encrypt(const uint8_t *key,     /* encryption key               */
                     const uint8_t iVec[16], /* initialization vector        */
                     const uint8_t *pntxt,   /* plaintext buffer             */
                     const size_t ptextLen,  /* length of input plain text   */
                     uint8_t *crtxt);        /* cipher-text result           */

void AES_OFB_decrypt(const uint8_t *key,     /* decryption key               */
                     const uint8_t iVec[16], /* initialization vector        */
                     const uint8_t *crtxt,   /* cipher-text buffer           */
                     const size_t crtxtLen,  /* length of input cipher text  */
                     uint8_t *pntxt);        /* plaintext result             */
#endif                                       /* OFB */

/**----------------------------------------------------------------------------
Main functions for XTS-AES block ciphering
 -----------------------------------------------------------------------------*/
#if XTS
char AES_XTS_encrypt(const uint8_t *keys,   /* encryption key pair          */
                     const uint8_t *tweak,  /* tweak value (unit/sector ID) */
                     const uint8_t *pntxt,  /* plaintext buffer             */
                     const size_t ptextLen, /* length of input plain text   */
                     uint8_t *crtxt);       /* cipher-text result           */

char AES_XTS_decrypt(const uint8_t *keys,   /* decryption key pair          */
                     const uint8_t *tweak,  /* tweak value (unit/sector ID) */
                     const uint8_t *crtxt,  /* cipher-text buffer           */
                     const size_t crtxtLen, /* length of input cipher text  */
                     uint8_t *pntxt);       /* plaintext result             */
#endif                                      /* XTS */

/**----------------------------------------------------------------------------
Main functions for CTR-AES block ciphering
 -----------------------------------------------------------------------------*/
#if CTR_NA
void AES_CTR_encrypt(const uint8_t *key,    /* encryption key               */
                     const uint8_t *iv,     /* initialization vector/ nonce */
                     const uint8_t *pntxt,  /* plaintext buffer             */
                     const size_t ptextLen, /* length of input plain text   */
                     uint8_t *crtxt);       /* cipher-text result           */

void AES_CTR_decrypt(const uint8_t *key,    /* decryption key               */
                     const uint8_t *iv,     /* initialization vector/ nonce */
                     const uint8_t *crtxt,  /* cipher-text buffer           */
                     const size_t crtxtLen, /* length of input cipher text  */
                     uint8_t *pntxt);       /* plaintext result             */
#endif                                      /* CTR */

/**----------------------------------------------------------------------------
Main functions for SIV-AES block ciphering
 -----------------------------------------------------------------------------*/
#if SIV
void AES_SIV_encrypt(const uint8_t *keys,   /* encryption key pair          */
                     const uint8_t *pntxt,  /* plain text                   */
                     const size_t ptextLen, /* length of input plain text   */
                     const uint8_t *aData,  /* added authentication data    */
                     const size_t aDataLen, /* size of authentication data  */
                     uint8_t iv[16],        /* synthesized initial-vector   */
                     uint8_t *crtxt);       /* cipher-text result           */

char AES_SIV_decrypt(const uint8_t *keys,   /* decryption key pair          */
                     const uint8_t iv[16],  /* provided initial-vector      */
                     const uint8_t *crtxt,  /* cipher text                  */
                     const size_t crtxtLen, /* length of input cipher-text  */
                     const uint8_t *aData,  /* added authentication data    */
                     const size_t aDataLen, /* size of authentication data  */
                     uint8_t *pntxt);       /* plain-text result            */
#endif                                      /* SIV */

/**----------------------------------------------------------------------------
Main functions for GCM-AES block ciphering
 -----------------------------------------------------------------------------*/
#if GCM
void AES_GCM_encrypt(const uint8_t *key,    /* encryption key               */
                     const uint8_t *nonce,  /* a.k.a initialization vector  */
                     const uint8_t *pntxt,  /* plain text                   */
                     const size_t ptextLen, /* length of input plain text   */
                     const uint8_t *aData,  /* added authentication data    */
                     const size_t aDataLen, /* size of authentication data  */
                     uint8_t *crtxt,        /* cipher-text result           */
                     uint8_t auTag[16]);    /* message authentication tag   */

char AES_GCM_decrypt(const uint8_t *key,    /* decryption key               */
                     const uint8_t *nonce,  /* a.k.a initialization vector  */
                     const uint8_t *crtxt,  /* cipher text + appended tag   */
                     const size_t crtxtLen, /* length of input cipher-text  */
                     const uint8_t *aData,  /* added authentication data    */
                     const size_t aDataLen, /* size of authentication data  */
                     const uint8_t tagLen,  /* size of tag (if any)         */
                     uint8_t *pntxt);       /* plain-text result            */
#endif                                      /* GCM */

/**----------------------------------------------------------------------------
Main functions for CCM-AES block ciphering
 -----------------------------------------------------------------------------*/
#if CCM
void AES_CCM_encrypt(const uint8_t *key,    /* encryption key               */
                     const uint8_t *nonce,  /* a.k.a initialization vector  */
                     const uint8_t *pntxt,  /* plain text                   */
                     const size_t ptextLen, /* length of input plain text   */
                     const uint8_t *aData,  /* added authentication data    */
                     const size_t aDataLen, /* size of authentication data  */
                     uint8_t *crtxt,        /* cipher-text result           */
                     uint8_t auTag[16]);    /* message authentication tag   */

char AES_CCM_decrypt(const uint8_t *key,    /* decryption key               */
                     const uint8_t *nonce,  /* a.k.a initialization vector  */
                     const uint8_t *crtxt,  /* cipher text + appended tag   */
                     const size_t crtxtLen, /* length of input cipher-text  */
                     const uint8_t *aData,  /* added authentication data    */
                     const size_t aDataLen, /* size of authentication data  */
                     const uint8_t tagLen,  /* size of tag (if any)         */
                     uint8_t *pntxt);       /* plain-text result            */
#endif                                      /* CCM */

/**----------------------------------------------------------------------------
Main functions for OCB-AES block ciphering
 -----------------------------------------------------------------------------*/
#if OCB
void AES_OCB_encrypt(const uint8_t *key,    /* encryption key               */
                     const uint8_t *nonce,  /* a.k.a initialization vector  */
                     const uint8_t *pntxt,  /* plain text                   */
                     const size_t ptextLen, /* length of input plain text   */
                     const uint8_t *aData,  /* added authentication data    */
                     const size_t aDataLen, /* size of authentication data  */
                     uint8_t *crtxt,        /* cipher-text result           */
                     uint8_t auTag[16]);    /* message authentication tag   */

char AES_OCB_decrypt(const uint8_t *key,    /* decryption key               */
                     const uint8_t *nonce,  /* a.k.a initialization vector  */
                     const uint8_t *crtxt,  /* cipher text + appended tag   */
                     const size_t crtxtLen, /* length of input cipher-text  */
                     const uint8_t *aData,  /* added authentication data    */
                     const size_t aDataLen, /* size of authentication data  */
                     const uint8_t tagLen,  /* size of tag (if any)         */
                     uint8_t *pntxt);       /* plain-text result            */
#endif                                      /* OCB */

/**----------------------------------------------------------------------------
Main functions for EAX-AES mode; more info at the bottom of this document.
 -----------------------------------------------------------------------------*/
#if EAX
void AES_EAX_encrypt(const uint8_t *key,    /* encryption key               */
                     const uint8_t *nonce,  /* arbitrary-size nonce array   */
                     const uint8_t *pntxt,  /* plain text                   */
                     const size_t ptextLen, /* length of input plain text   */
#if EAXP
                     const size_t nonceLen, /* size of provided nonce       */
                     uint8_t *crtxt);       /* cipher-text result + mac (4) */
#else
                     const uint8_t *aData,  /* added authentication data    */
                     const size_t aDataLen, /* size of authentication data  */
                     uint8_t *crtxt,        /* cipher-text result           */
                     uint8_t auTag[16]);    /* message authentication tag   */
#endif

char AES_EAX_decrypt(const uint8_t *key,    /* decryption key               */
                     const uint8_t *nonce,  /* arbitrary-size nonce array   */
                     const uint8_t *crtxt,  /* cipher text + appended tag   */
                     const size_t crtxtLen, /* length of input cipher-text  */
#if EAXP
                     const size_t nonceLen, /* size of provided nonce       */
#else
                     const uint8_t *aData,  /* added authentication data    */
                     const size_t aDataLen, /* size of authentication data  */
                     const uint8_t tagLen,  /* size of tag (if any)         */
#endif
                     uint8_t *pntxt); /* plain-text result            */
#endif                                /* EAX */

/**----------------------------------------------------------------------------
Main functions for GCM-SIV-AES block ciphering
 -----------------------------------------------------------------------------*/
#if GCM_SIV
void GCM_SIV_encrypt(const uint8_t *key,    /* encryption key               */
                     const uint8_t *nonce,  /* provided 96-bit nonce        */
                     const uint8_t *pntxt,  /* plain text                   */
                     const size_t ptextLen, /* length of input plain text   */
                     const uint8_t *aData,  /* added authentication data    */
                     const size_t aDataLen, /* size of authentication data  */
                     uint8_t *crtxt,        /* cipher-text result           */
                     uint8_t auTag[16]);    /* 16-bytes mandatory tag       */

char GCM_SIV_decrypt(const uint8_t *key,    /* decryption key               */
                     const uint8_t *nonce,  /* provided 96-bit nonce        */
                     const uint8_t *crtxt,  /* cipher text + appended tag   */
                     const size_t crtxtLen, /* length of input cipher-text  */
                     const uint8_t *aData,  /* added authentication data    */
                     const size_t aDataLen, /* size of authentication data  */
                     const uint8_t tagLen,  /* size of tag (must be 16)     */
                     uint8_t *pntxt);       /* plain-text result            */
#endif                                      /* GCM-SIV */

/**----------------------------------------------------------------------------
Main functions for AES key-wrapping
 -----------------------------------------------------------------------------*/
#if KWA
char AES_KEY_wrap(const uint8_t *kek,     /* key encryption key           */
                  const uint8_t *secret,  /* input secret to be wrapped   */
                  const size_t secretLen, /* size of input                */
                  uint8_t *wrapped);      /* key-wrapped output           */

char AES_KEY_unwrap(const uint8_t *kek,     /* key encryption key           */
                    const uint8_t *wrapped, /* key-wrapped secret           */
                    const size_t wrapLen,   /* size of input (secretLen +8) */
                    uint8_t *secret);       /* buffer for unwrapped key     */
#endif                                      /* KWA */

/**----------------------------------------------------------------------------
Main functions for FPE-AES; more info at the bottom of this page.
 -----------------------------------------------------------------------------*/
#if FPE
char AES_FPE_encrypt(const uint8_t *key,   /* encryption key               */
                     const uint8_t *tweak, /* tweak bytes                  */
#if FF_X == 3
#define FF3_TWEAK_LEN 7 /* set 7 for FF3-1, or 8 if FF3 */
#else
                     const size_t tweakLen, /* size of tweak array          */
#endif
                     const void *pntxt,     /* input plaintext string       */
                     const size_t ptextLen, /* length of plaintext string   */
                     void *crtxt);          /* cipher-text result           */

char AES_FPE_decrypt(const uint8_t *key,   /* decryption key               */
                     const uint8_t *tweak, /* tweak bytes                  */
#if FF_X != 3
                     const size_t tweakLen, /* size of tweak array          */
#endif
                     const void *crtxt,     /* input ciphertext string      */
                     const size_t crtxtLen, /* length of ciphertext string  */
                     void *pntxt);          /* plain-text result            */
#endif                                      /* FPE */

/**----------------------------------------------------------------------------
Main function for Poly1305-AES message authentication code
 -----------------------------------------------------------------------------*/
#if POLY1305
void AES_Poly1305(const uint8_t *keys,     /* encryption/mixing key pair   */
                  const uint8_t nonce[16], /* the 128-bit nonce            */
                  const void *data,        /* input data buffer            */
                  const size_t dataSize,   /* size of data in bytes        */
                  uint8_t mac[16]);        /* poly1305-AES mac of data     */
#endif

/**----------------------------------------------------------------------------
Main function for AES Cipher-based Message Authentication Code
 -----------------------------------------------------------------------------*/
#if CMAC
void AES_CMAC(const uint8_t *key,    /* encryption/cipher key        */
              const void *data,      /* input data buffer            */
              const size_t dataSize, /* size of data in bytes        */
              uint8_t mac[16]);      /* CMAC result of input data    */
#endif

/**----------------------------------------------------------------------------
The error codes and key length should be defined here for external references:
 -----------------------------------------------------------------------------*/
#define ENCRYPTION_FAILURE 0x1E
#define DECRYPTION_FAILURE 0x1D
#define AUTHENTICATION_FAILURE 0x1A
#define ENDED_IN_SUCCESS 0x00

#if (AES___ != 256) && (AES___ != 192)
#define AES_KEY_SIZE 16
#else
#define AES_KEY_SIZE (AES___ / 8)
#endif

/******************************************************************************\
¦               Notes and remarks about the above-defined macros               ¦
--------------------------------------------------------------------------------

* In EBC/CBC/XEX modes, the size of input must be a multiple of block-size.
    Otherwise it needs to be padded. The simplest (default) padding mode is to
    fill the rest of block by zeros. Supported standard padding methods are
    PKCS#7 and ISO/IEC 7816-4, which can be enabled by the AES_PADDING macro.

* In many texts, you may see that the words 'nonce' and 'initialization vector'
    are used interchangeably. But they have a subtle difference. Sometimes nonce
    is a part of the I.V, which itself can either be a full block or a partial
    one. In CBC/CFB/OFB modes, the provided I.V must be a full block. In pure
    CTR mode (CTR_NA) you can either provide a 96-bit I.V and let the count
    start at CTR_STARTVALUE, or use a full block IV.

* In AEAD modes, the size of nonce and tag might be a parameter of the algorithm
    such that changing them affect the results. The GCM/EAX modes support
    arbitrary sizes for nonce. In CCM, the nonce length may vary from 8 to 13
    bytes. Also the tag size is an EVEN number between 4..16. In OCB, the nonce
    size is 1..15 and the tag is 0..16 bytes. Note that the 'calculated' tag-
    size is always 16 bytes which can later be truncated to desired values. So
    in encryption functions, the provided authTag buffer must be 16 bytes long.

* For the EAX mode of operation, the IEEE-1703 standard defines EAX' which is a
    modified version that combines AAD and nonce. Also the tag size is fixed to
    4 bytes. So EAX-prime functions don't need to take additional authentication
    data and tag-size as separate parameters.

* In SIV mode, multiple separate units of authentication headers can be provided
    for the nonce synthesis. Here we assume that only one unit of AAD (aData) is
    sufficient, which is practically true.

* The FPE mode has two distinct NIST-approved algorithms, namely FF1 and FF3-1.
    Use the FF_X macro to change the encryption method, which is FF1 by default.
    The input and output strings must be consisted of a fixed set of characters
    called 'the alphabet'. The default alphabet is the set of digits {'0'..'9'}.
    If you want to use a different alphabet, set the CUSTOM_ALPHABET macro and
    refer to the "micro_fpe.h" header. This file is needed only when a custom
    alphabet has to be defined, and contains some illustrative examples and
    clear guidelines on how to do so.

* The key wrapping mode is also denoted by KW. In this mode, the input secret is
    divided into 64bit blocks. Number of blocks is at least 2, and it is assumed
    that no padding is required. For padding, the KWP mode must be used which is
    easily implementable, but left as an exercise! In the NIST document you may
    find some mentions of TKW which is for 3DES and irrelevant here. Anyway, the
    wrapped output has an additional block, i.e. wrappedSize = secretSize + 8.

* Let me explain three extra options that are defined in the source file. If the
    length of the input cipher/plain text is 'always' less than 4KB, you can
    enable the SMALL_CIPHER macro to save a few bytes in the compiled code. This
    assumption is likely to be valid for some embedded systems and small-scale
    applications. Furthermore, disabling that other macro, DONT_USE_FUNCTIONS
    had a considerable effect on the size of the compiled code in my own tests.
    Nonetheless, others might get a different result from them.

    The INCREASE_SECURITY macro, as its name suggests, is dealing with security
    considerations. For example, since the RoundKey is declared as static array
    it might get exposed to some attacks. By enabling this macro, round-keys are
    wiped out at the end of ciphering operations. However, please keep in mind
    that this is NOT A GUARANTEE against side-channel attacks.

*/

/*----------------------------------------------------------------------------*\
          Global constants, data types, and important / useful MACROs
\*----------------------------------------------------------------------------*/

#define KEYSIZE AES_KEY_SIZE
#define BLOCKSIZE (128 / 8) /* Block length in AES is 'always' 128-bits.    */
#define Nb (BLOCKSIZE / 4)  /* The number of columns comprising a AES state */
#define Nk (KEYSIZE / 4)    /* The number of 32 bit words in a key.         */
#define ROUNDS (Nk + 6)     /* The number of rounds in AES Cipher.          */

#define IMPLEMENT(x) (x) > 0

#define INCREASE_SECURITY 0
#define DONT_USE_FUNCTIONS 0
#define SMALL_CIPHER 0 /* for more info, see the bottom of header file */

/** block_t indicates fixed-size memory blocks, and state_t represents the state
 * matrix. note that state[i][j] means the i-th COLUMN and j-th ROW of matrix */
typedef uint8_t block_t[BLOCKSIZE];
typedef uint8_t state_t[Nb][4];

/*----------------------------------------------------------------------------*\
                               Private variables:
\*----------------------------------------------------------------------------*/

/** The array that stores all round keys during the AES key-expansion process */
static uint8_t RoundKey[BLOCKSIZE * ROUNDS + KEYSIZE];

/** Lookup-tables are static constant, so that they can be placed in read-only
 * storage instead of RAM. They can be computed dynamically trading ROM for RAM.
 * This may be useful in (embedded) bootloader applications, where ROM is often
 * limited. Note that sbox[y] = x, if and only if rsbox[x] = y. You may read the
 * wikipedia article for more info: https://en.wikipedia.org/wiki/Rijndael_S-box
 */
static const char sbox[256] =
    "c|w{\362ko\3050\1g+\376\327\253\x76\312\202\311}\372YG\360\255\324\242\257"
    "\234\244r\300\267\375\223&6\?\367\3144\245\345\361q\3301\25\4\307#\303\030"
    "\226\5\232\a\22\200\342\353\'\262u\t\203,\32\33nZ\240R;\326\263)\343/\204S"
    "\321\0\355 \374\261[j\313\2769JLX\317\320\357\252\373CM3\205E\371\02\177P<"
    "\237\250Q\243@\217\222\2358\365\274\266\332!\20\377\363\322\315\f\023\354_"
    "\227D\27\304\247~=d]\31s`\201O\334\"*\220\210F\356\270\24\336^\v\333\3402:"
    "\nI\06$\\\302\323\254b\221\225\344y\347\3107m\215\325N\251lV\364\352ez\256"
    "\b\272x%.\034\246\264\306\350\335t\37K\275\213\212p>\265fH\3\366\16a5W\271"
    "\206\301\035\236\341\370\230\21i\331\216\224\233\036\207\351\316U(\337\214"
    "\241\211\r\277\346BhA\231-\17\260T\273\26";

#if DECRYPTION
static const char rsbox[256] =
    "R\tj\32506\2458\277@\243\236\201\363\327\373|\3439\202\233/\377\2074\216CD"
    "\304\336\351\313T{\2242\246\302#=\356L\225\vB\372\303N\b.\241f(\331$\262v["
    "\242Im\213\321%r\370\366d\206h\230\026\324\244\\\314]e\266\222lpHP\375\355"
    "\271\332^\25FW\247\215\235\204\220\330\253\0\214\274\323\n\367\344X\05\270"
    "\263E\6\320,\036\217\312?\17\2\301\257\275\3\1\023\212k:\221\21AOg\334\352"
    "\227\362\317\316\360\264\346s\226\254t\"\347\2555\205\342\3717\350\34u\337"
    "nG\361\32q\35)\305\211o\267b\16\252\30\276\33\374V>K\306\322y \232\333\300"
    "\376x\315Z\364\037\335\2503\210\a\3071\261\22\20Y\'\200\354_`Q\177\251\031"
    "\265J\r-\345z\237\223\311\234\357\240\340;M\256*\365\260\310\353\273<\203S"
    "\231a\27+\4~\272w\326&\341i\24cU!\f}";
#endif

/*----------------------------------------------------------------------------*\
                 Auxiliary functions for the Rijndael algorithm
\*----------------------------------------------------------------------------*/

#define SBoxValue(x) (sbox[x])
#define InvSBoxValue(x) (rsbox[x]) /* omitted dynamic s-box calculation */

#define COPY32BIT(x, y) *(int32_t *) &(y) = *(int32_t *) &x
#define XOR32BITS(x, y) *(int32_t *) &(y) ^= *(int32_t *) &x

#if DONT_USE_FUNCTIONS

/** note: 'long long' type is NOT supported in C89. so this may throw errors: */
#define xorBlock(x, y)                                          \
    {                                                           \
        *(long long *) &(y)[0] ^= *(long long const *) &(x)[0]; \
        *(long long *) &(y)[8] ^= *(long long const *) &(x)[8]; \
    }

#define xtime(x) (x & 0x80 ? x << 1 ^ 0x11b : x * 2)

#define gmul(x, y)                               \
    (x * (y <= 13)) ^ (xtime(x) * (y / 2 & 1)) ^ \
        (xtime(xtime(x)) * (y >= 13)) ^ (xtime(xtime(xtime(x))))
#else

/** XOR two 128bit numbers (blocks) called src and dest, so that: dest ^= src */
static void xorBlock(const block_t src, block_t dest)
{
    uint8_t i;
    for (i = 0; i < BLOCKSIZE; ++i) /* many CPUs have single instruction */
    {                               /*  such as XORPS for 128-bit-xor.   */
        dest[i] ^= src[i];          /* see the file: x86-improvements    */
    }
}

/** doubling in GF(2^8): left-shift and if carry bit is set, xor it with 0x1b */
static uint8_t xtime(uint8_t x)
{
    return (x > 0x7f) * 0x1b ^ (x << 1);
}

#if DECRYPTION

/** This function multiplies two numbers in the Galois bit field of GF(2^8).. */
static uint8_t gmul(uint8_t x, uint8_t y)
{
    uint8_t m;
    for (m = 0; y > 1; y >>= 1) /* optimized algorithm for nonzero y */
    {
        if (y & 01)
            m ^= x;
        x = xtime(x);
    }
    return m ^ x; /* or use (9 11 13 14) lookup tables */
}
#endif
#endif

/*----------------------------------------------------------------------------*\
              Main functions for the Rijndael encryption algorithm
\*----------------------------------------------------------------------------*/

/** This function produces (ROUNDS+1) round keys, which are used in each round
 * to encrypt/decrypt the intermediate states. First round key is the main key
 * itself, and other rounds are constructed from the previous ones as follows */
static void KeyExpansion(const uint8_t *key)
{
    uint8_t rcon = 1, i;
    memcpy(RoundKey, key, KEYSIZE);

    for (i = KEYSIZE; i < (ROUNDS + 1) * Nb * 4; i += 4) {
        switch (i % KEYSIZE) {
        case 0:
            memcpy(&RoundKey[i], &RoundKey[i - KEYSIZE], KEYSIZE);
#if Nk == 4
            if (!rcon)
                rcon = 0x1b; /* RCON may reach 0 only in AES-128. */
#endif
            RoundKey[i] ^= SBoxValue(RoundKey[i - 3]) ^ rcon;
            RoundKey[i + 1] ^= SBoxValue(RoundKey[i - 2]);
            RoundKey[i + 2] ^= SBoxValue(RoundKey[i - 1]);
            RoundKey[i + 3] ^= SBoxValue(RoundKey[i - 4]);
            rcon <<= 1;
            break;
#if Nk == 8 /* additional round only for AES-256 */
        case 16:
            RoundKey[i] ^= SBoxValue(RoundKey[i - 4]);
            RoundKey[i + 1] ^= SBoxValue(RoundKey[i - 3]);
            RoundKey[i + 2] ^= SBoxValue(RoundKey[i - 2]);
            RoundKey[i + 3] ^= SBoxValue(RoundKey[i - 1]);
            break;
#endif
        default:
            XOR32BITS(RoundKey[i - 0x4], RoundKey[i]);
            break;
        }
    }
}

/** Add the round keys to the rijndael state matrix (adding in GF means XOR). */
static void AddRoundKey(const uint8_t round, block_t state)
{
    xorBlock(RoundKey + BLOCKSIZE * round, state);
}

/** Substitute values in the state matrix with associated values in the S-box */
static void SubBytes(block_t state)
{
    uint8_t i;
    for (i = 0; i < BLOCKSIZE; ++i) {
        state[i] = SBoxValue(state[i]);
    }
}

/** Shift/rotate the rows of the state matrix to the left. Each row is shifted
 * with a different offset (= Row number). So the first row is not shifted .. */
static void ShiftRows(state_t *state)
{
    uint8_t temp = (*state)[0][1];
    (*state)[0][1] = (*state)[1][1];
    (*state)[1][1] = (*state)[2][1];
    (*state)[2][1] = (*state)[3][1];
    (*state)[3][1] = temp; /*  Rotated the 1st row 1 columns to left */

    temp = (*state)[0][2];
    (*state)[0][2] = (*state)[2][2];
    (*state)[2][2] = temp;
    temp = (*state)[1][2];
    (*state)[1][2] = (*state)[3][2];
    (*state)[3][2] = temp; /*  Rotated the 2nd row 2 columns to left */

    temp = (*state)[0][3];
    (*state)[0][3] = (*state)[3][3];
    (*state)[3][3] = (*state)[2][3];
    (*state)[2][3] = (*state)[1][3];
    (*state)[1][3] = temp; /*  Rotated the 3rd row 3 columns to left */
}

/** Mix the columns of the state matrix. See: crypto.stackexchange.com/q/2402 */
static void MixColumns(state_t *state)
{
    uint8_t a, b, c, d, i;
    for (i = 0; i < Nb; ++i) {
        a = (*state)[i][0] ^ (*state)[i][1];
        b = (*state)[i][1] ^ (*state)[i][2];
        c = (*state)[i][2] ^ (*state)[i][3];

        d = a ^ c; /*  d is XOR of all elements in a column  */
        (*state)[i][0] ^= d ^ xtime(a);
        (*state)[i][1] ^= d ^ xtime(b);

        b ^= d; /* -> b = (*state)[i][3] ^ (*state)[i][0] */
        (*state)[i][2] ^= d ^ xtime(c);
        (*state)[i][3] ^= d ^ xtime(b);
    }
}

/** Encrypt a plaintext input block and save the result/ciphertext as output. */
static void rijndaelEncrypt(const block_t input, block_t output)
{
    uint8_t r;
    state_t *state = (void *) output;

    /* copy the input to the state matrix, and beware of undefined behavior.. */
    if (input != output)
        memcpy(state, input, BLOCKSIZE);

    /* The encryption is carried out in #ROUNDS iterations, of which the first
     * #ROUNDS-1 are identical. The last round doesn't involve mixing columns */
    for (r = 0; r != ROUNDS;) {
        AddRoundKey(r, output);
        SubBytes(output);
        ShiftRows(state);
        ++r != ROUNDS ? MixColumns(state) : AddRoundKey(ROUNDS, output);
    }
}

/*----------------------------------------------------------------------------*\
                Block-decryption part of the Rijndael algorithm
\*----------------------------------------------------------------------------*/

#if IMPLEMENT(DECRYPTION)

/** Substitutes the values in state matrix with values of the inverted S-box. */
static void InvSubBytes(block_t state)
{
    uint8_t i;
    for (i = 0; i < BLOCKSIZE; ++i) {
        state[i] = InvSBoxValue(state[i]);
    }
}

/** This function shifts/rotates the rows of the state matrix to the right .. */
static void InvShiftRows(state_t *state)
{
    uint8_t temp = (*state)[3][1];
    (*state)[3][1] = (*state)[2][1];
    (*state)[2][1] = (*state)[1][1];
    (*state)[1][1] = (*state)[0][1];
    (*state)[0][1] = temp; /*  Rotated first row 1 columns to right  */

    temp = (*state)[0][2];
    (*state)[0][2] = (*state)[2][2];
    (*state)[2][2] = temp;
    temp = (*state)[1][2];
    (*state)[1][2] = (*state)[3][2];
    (*state)[3][2] = temp; /*  Rotated second row 2 columns to right */

    temp = (*state)[0][3];
    (*state)[0][3] = (*state)[1][3];
    (*state)[1][3] = (*state)[2][3];
    (*state)[2][3] = (*state)[3][3];
    (*state)[3][3] = temp; /*  Rotated third row 3 columns to right  */
}

/** Mixes the columns of (already-mixed) state matrix to reverse the process. */
static void InvMixColumns(state_t *state)
{
    uint8_t i, x[4];
    for (i = 0; i < Nb; ++i) /*  see: crypto.stackexchange.com/q/2569  */
    {
        COPY32BIT((*state)[i][0], x[0]);

        (*state)[i][0] =
            gmul(x[0], 14) ^ gmul(x[1], 11) ^ gmul(x[2], 13) ^ gmul(x[3], 9);
        (*state)[i][1] =
            gmul(x[1], 14) ^ gmul(x[2], 11) ^ gmul(x[3], 13) ^ gmul(x[0], 9);
        (*state)[i][2] =
            gmul(x[2], 14) ^ gmul(x[3], 11) ^ gmul(x[0], 13) ^ gmul(x[1], 9);
        (*state)[i][3] =
            gmul(x[3], 14) ^ gmul(x[0], 11) ^ gmul(x[1], 13) ^ gmul(x[2], 9);
    }
}

/** Decrypt a ciphertext input block and save the result/plaintext to output. */
static void rijndaelDecrypt(const block_t input, block_t output)
{
    uint8_t r;
    state_t *state = (void *) output;

    /* copy the input into state matrix, i.e. state is initialized by input.. */
    if (input != output)
        memcpy(state, input, BLOCKSIZE);

    /* Decryption completes after #ROUNDS iterations. All rounds except the 1st
     * one are identical. The first round doesn't involve [inv]mixing columns */
    for (r = ROUNDS; r != 0;) {
        r-- != ROUNDS ? InvMixColumns(state) : AddRoundKey(ROUNDS, output);
        InvShiftRows(state);
        InvSubBytes(output);
        AddRoundKey(r, output);
    }
}
#endif /* DECRYPTION */


#if M_RIJNDAEL
/**
 * @brief   encrypt or decrypt a single block with a given key
 * @param   key       a byte array with a fixed size of KEYSIZE
 * @param   mode      mode of operation: 'E' (1) to encrypt, 'D' (0) to decrypt
 * @param   x         input byte array with BLOCKSIZE bytes
 * @param   y         output byte array with BLOCKSIZE bytes
 */
void AES_Cipher(const uint8_t *key, const char mode, const block_t x, block_t y)
{
    KeyExpansion(key);
    mode & 1 ? rijndaelEncrypt(x, y) : rijndaelDecrypt(x, y);
}
#endif

/*----------------------------------------------------------------------------*\
 *              Implementation of different block ciphers modes               *
 *                     Definitions & Auxiliary Functions                      *
\*----------------------------------------------------------------------------*/

#define AES_SetKey(key) KeyExpansion(key)

#if INCREASE_SECURITY
#define BURN(key) memset(key, 0, sizeof key)
#define SABOTAGE(buf, len) memset(buf, 0, len)
#define MISMATCH constmemcmp /*  a.k.a secure memcmp      */
#else
#define MISMATCH memcmp
#define SABOTAGE(buf, len) (void) buf
#define BURN(key) (void) key /*  the line is ignored.     */
#endif

#if INCREASE_SECURITY && AEAD_MODES

/** for constant-time comparison of memory blocks, to avoid "timing attacks". */
static uint8_t constmemcmp(const uint8_t *src, const uint8_t *dst, uint8_t n)
{
    uint8_t cmp = 0;
    while (n--) {
        cmp |= src[n] ^ dst[n];
    }
    return cmp;
}
#endif

/** function-pointer types, indicating functions that take fixed-size blocks: */
typedef void (*fdouble_t)(block_t);
typedef void (*fmix_t)(const block_t, block_t);

#define LAST (BLOCKSIZE - 1) /*  last index in a block    */

#if SMALL_CIPHER
typedef uint8_t count_t;

#define incBlock(block, big) ++block[big ? LAST : 0]
#define xor2BVal(buf, num, pos) \
    buf[pos - 1] ^= (num) >> 8; \
    buf[pos] ^= num
#define copyLVal(buf, num, pos) \
    buf[pos + 1] = (num) >> 8;  \
    buf[pos] = num

#else
typedef size_t count_t;

#if CTR || KWA || FPE

/** xor a byte array with a big-endian integer, whose LSB is at specified pos */
static void xor2BVal(uint8_t *buff, size_t val, uint8_t pos)
{
    do
        buff[pos--] ^= (uint8_t) val;
    while (val >>= 8);
}
#endif

#if XTS || GCM_SIV

/** copy a little endian integer to the block, with LSB at specified position */
static void copyLVal(block_t block, size_t val, uint8_t pos)
{
    do
        block[pos++] = (uint8_t) val;
    while (val >>= 8);
}
#endif

#if CTR

/** increment the value of a 128-bit counter block, regarding its endian-ness */
static void incBlock(block_t block, const char big)
{
    uint8_t i = big ? LAST : 0;
    if (i) /*  big-endian counter       */
    {
        while (!++block[i])
            --i; /* (inc until no overflow)   */
    } else {
        while (i < 4 && !++block[i])
            ++i;
    }
}
#endif
#endif /* SMALL CIPHER */

#if EAX && !EAXP || SIV || OCB || CMAC

/** Multiply a block by two in Galois bit field GF(2^128): big-endian version */
static void doubleBGF128(block_t block)
{
    int i, c = 0;
    for (i = BLOCKSIZE; i; c >>= 8) /* from last byte (LSB) to   */
    {                               /* first: left-shift, then   */
        c |= block[--i] << 1;       /* append the previous MSBit */
        block[i] = (uint8_t) c;
    } /* if first MSBit is carried */
    block[LAST] ^= c * 0x87; /* .. B ^= 10000111b (B.E.)  */
}
#endif

#if XTS || EAXP

/** Multiply a block by two in the GF(2^128) field: the little-endian version */
static void doubleLGF128(block_t block)
{
    int c = 0, i;
    for (i = 0; i < BLOCKSIZE; c >>= 8) /* the same as doubleBGF128  */
    {                                   /* ..but with reversed bytes */
        c |= block[i] << 1;
        block[i++] = (uint8_t) c;
    }
    block[0] ^= c * 0x87; /*    B ^= 10000111b (L.E.)  */
}
#endif

#if GCM

/** Divide a block by two in GF(2^128) field: used in big endian, 128bit mul. */
static void halveBGF128(block_t block)
{
    unsigned i, c = 0;
    for (i = 0; i < BLOCKSIZE; c <<= 8) /* from first to last byte,  */
    {                                   /*  prepend the previous LSB */
        c |= block[i];                  /*  then shift it to right.  */
        block[i++] = (uint8_t) (c >> 1);
    } /* if block is odd (LSB = 1) */
    if (c & 0x100)
        block[0] ^= 0xe1; /* .. B ^= 11100001b << 120  */
}

/** This function carries out multiplication in 128bit Galois field GF(2^128) */
static void mulGF128(const block_t x, block_t y)
{
    uint8_t i, j, result[BLOCKSIZE] = {0}; /*  working memory           */

    for (i = 0; i < BLOCKSIZE; ++i) {
        for (j = 0; j < 8; ++j) /*  check all the bits of X, */
        {
            if (x[i] << j & 0x80) /*  ..and if any bit is set, */
            {
                xorBlock(y, result); /*  ..add Y to the result    */
            }
            halveBGF128(y); /*  Y_next = (Y / 2) in GF   */
        }
    }
    memcpy(y, result, sizeof result); /*  result is saved into y   */
}
#endif /* GCM */

#if GCM_SIV

/** Divide a block by two in GF(2^128) field: the little-endian version (duh) */
static void halveLGF128(block_t block)
{
    unsigned c = 0, i;
    for (i = BLOCKSIZE; i; c <<= 8) /* the same as halveBGF128 ↑ */
    {                               /* ..but with reversed bytes */
        c |= block[--i];
        block[i] = (uint8_t) (c >> 1);
    }
    if (c & 0x100)
        block[LAST] ^= 0xe1; /* B ^= LE. 11100001b << 120 */
}

/** Dot multiplication in GF(2^128) field: used in POLYVAL hash for GCM-SIV.. */
static void dotGF128(const block_t x, block_t y)
{
    uint8_t i, j, result[BLOCKSIZE] = {0};

    for (i = BLOCKSIZE; i--;) {
        for (j = 8; j--;) /*  pretty much the same as  */
        {                 /*  ..(reversed) mulGF128    */
            halveLGF128(y);
            if (x[i] >> j & 1) {
                xorBlock(y, result);
            }
        }
    }
    memcpy(y, result, sizeof result); /*  result is saved into y   */
}
#endif /* GCM-SIV */

#if CBC || CFB || OFB || CTR || OCB

/** Result of applying a function to block `b` is xor-ed with `x` to get `y`. */
static void mixThenXor(fmix_t mix,
                       const block_t b,
                       block_t f,
                       const uint8_t *x,
                       const uint8_t len,
                       uint8_t *y)
{
    uint8_t i = len;
    if (len) {
        mix(b, f); /*       Y = f(B) ^ X        */
        while (i--) {
            y[i] = f[i] ^ x[i];
        }
    }
}
#endif

#if AEAD_MODES || FPE

/** xor the result with input data and then apply the digest/mixing function.
 * repeat the process for each block of data until all blocks are digested... */
static void xMac(const void *data,
                 const size_t dataSize,
                 const block_t seed,
                 fmix_t mix,
                 block_t result)
{
    uint8_t const *x;
    count_t n = dataSize / BLOCKSIZE; /*   number of full blocks   */

    for (x = data; n--; x += BLOCKSIZE) {
        xorBlock(x, result); /* M_next = mix(seed, M ^ X) */
        mix(seed, result);
    }
    if ((n = dataSize % BLOCKSIZE) != 0) {
        while (n--) {
            result[n] ^= x[n];
        }
        mix(seed, result);
    }
}
#endif

#if CMAC || SIV || EAX || OCB

/** calculate the CMAC of input data using pre-calculated keys: D (K1) and Q. */
static void cMac(const block_t D,
                 const block_t Q,
                 const void *data,
                 const size_t dataSize,
                 block_t mac)
{
    const uint8_t r = dataSize ? (dataSize - 1) % BLOCKSIZE + 1 : 0;
    const uint8_t *p = r ? (uint8_t const *) data + dataSize - r : &r;

    xMac(data, dataSize - r, mac, &rijndaelEncrypt, mac);
    if (r < BLOCKSIZE) {
        mac[r] ^= 0x80;
    }
    xorBlock(r < BLOCKSIZE ? Q : D, mac); /*  pad( M; D, Q )           */

    xMac(p, r + !r, mac, &rijndaelEncrypt, mac);
}

/** calculate key-dependent constants D and Q using a given doubling function */
static void getSubkeys(fdouble_t fdouble,
                       const char quad,
                       const uint8_t *key,
                       block_t D,
                       block_t Q)
{
    AES_SetKey(key);
    rijndaelEncrypt(D, D); /*  H or L_* = Enc(zeros)    */
    if (quad) {
        fdouble(D); /*  D or L_$ = double(L_*)   */
    }
    memcpy(Q, D, BLOCKSIZE);
    fdouble(Q); /*  Q or L_0 = double(L_$)   */
}
#endif

#ifdef AES_PADDING

/** in ECB mode & CBC without CTS, the last (partial) block has to be padded. */
static char padBlock(const uint8_t len, block_t block)
{
#if AES_PADDING
    uint8_t n = BLOCKSIZE - len, *p = &block[len];
    memset(p, n * (AES_PADDING != 2), n);
    *p ^= '\x80' * (AES_PADDING == 2); /* either PKCS#7 / IEC7816-4 */
#else
    if (len) /* default (zero) padding    */
    {
        memset(block + len, 0, BLOCKSIZE - len);
    }
#endif
    return len || AES_PADDING;
}
#endif


/*----------------------------------------------------------------------------*\
                  ECB-AES (electronic codebook mode) functions
\*----------------------------------------------------------------------------*/
#if IMPLEMENT(ECB)
/**
 * @brief   encrypt the input plaintext using ECB-AES block-cipher method
 * @param   key       encryption key with a fixed size specified by KEYSIZE
 * @param   pntxt     input plaintext buffer
 * @param   ptextLen  size of plaintext in bytes
 * @param   crtxt     resulting cipher-text buffer
 */
void AES_ECB_encrypt(const uint8_t *key,
                     const uint8_t *pntxt,
                     const size_t ptextLen,
                     uint8_t *crtxt)
{
    uint8_t *y;
    count_t n = ptextLen / BLOCKSIZE; /*  number of full blocks    */
    memcpy(crtxt, pntxt, ptextLen);   /*  copy plaintext to output */

    AES_SetKey(key);
    for (y = crtxt; n--; y += BLOCKSIZE) {
        rijndaelEncrypt(y, y); /*  C = Enc(P)               */
    }
    if (padBlock(ptextLen % BLOCKSIZE, y)) {
        rijndaelEncrypt(y, y);
    }
    BURN(RoundKey);
}

/**
 * @brief   decrypt the input ciphertext using ECB-AES block-cipher method
 * @param   key       decryption key with a fixed size specified by KEYSIZE
 * @param   crtxt     input ciphertext buffer
 * @param   crtxtLen  size of ciphertext in bytes
 * @param   pntxt     resulting plaintext buffer
 * @return            whether size of ciphertext is a multiple of BLOCKSIZE
 */
char AES_ECB_decrypt(const uint8_t *key,
                     const uint8_t *crtxt,
                     const size_t crtxtLen,
                     uint8_t *pntxt)
{
    uint8_t *y;
    count_t n = crtxtLen / BLOCKSIZE;
    memcpy(pntxt, crtxt, crtxtLen); /*  do in-place decryption   */

    AES_SetKey(key);
    for (y = pntxt; n--; y += BLOCKSIZE) {
        rijndaelDecrypt(y, y); /*  P = Dec(C)               */
    }
    BURN(RoundKey);

    /* if padding is enabled, check whether the result is properly padded. error
     * must be thrown if it's not. we skip this here and just check the size. */
    return crtxtLen % BLOCKSIZE ? DECRYPTION_FAILURE : ENDED_IN_SUCCESS;
}
#endif /* ECB */


/*----------------------------------------------------------------------------*\
                   CBC-AES (cipher block chaining) functions
\*----------------------------------------------------------------------------*/
#if IMPLEMENT(CBC)
/**
 * @brief   encrypt the input plaintext using CBC-AES block-cipher method
 * @param   key       encryption key with a fixed size specified by KEYSIZE
 * @param   iVec      initialization vector
 * @param   pntxt     input plaintext buffer
 * @param   ptextLen  size of plaintext in bytes
 * @param   crtxt     resulting cipher-text buffer
 * @return            whether plaintext size is >= BLOCKSIZE for CTS mode
 */
char AES_CBC_encrypt(const uint8_t *key,
                     const block_t iVec,
                     const uint8_t *pntxt,
                     const size_t ptextLen,
                     uint8_t *crtxt)
{
    uint8_t const *iv = iVec;
    uint8_t r = ptextLen % BLOCKSIZE, *y;
    count_t n = ptextLen / BLOCKSIZE;
#if CTS
    if (n == 0)
        return ENCRYPTION_FAILURE; /* size of data >= BLOCKSIZE */

    if (r == 0 && --n)
        r = BLOCKSIZE;
    n += !n;
#endif
    memcpy(crtxt, pntxt, ptextLen); /*  do in-place encryption   */

    AES_SetKey(key);
    for (y = crtxt; n--; y += BLOCKSIZE) {
        xorBlock(iv, y);       /*  C = Enc(IV ^ P)          */
        rijndaelEncrypt(y, y); /*  IV_next = C              */
        iv = y;
    }

#if CTS /*  cipher-text stealing CS3 */
    if (r) {
        block_t yn = {0};
        memcpy(yn, y, r);            /*  backup the last chunk    */
        memcpy(y, y - BLOCKSIZE, r); /*  'steal' the cipher-text  */
        y -= BLOCKSIZE;              /*  ..to fill the last chunk */
        iv = yn;
#else
    if (padBlock(r, y)) {
#endif
        xorBlock(iv, y);
        rijndaelEncrypt(y, y);
    }
    BURN(RoundKey);
    return ENDED_IN_SUCCESS;
}

/**
 * @brief   decrypt the input ciphertext using CBC-AES block-cipher method
 * @param   key       decryption key with a fixed size specified by KEYSIZE
 * @param   iVec      initialization vector
 * @param   crtxt     input ciphertext buffer
 * @param   crtxtLen  size of ciphertext in bytes
 * @param   pntxt     resulting plaintext buffer
 * @return            whether size of ciphertext is a multiple of BLOCKSIZE
 */
char AES_CBC_decrypt(const uint8_t *key,
                     const block_t iVec,
                     const uint8_t *crtxt,
                     const size_t crtxtLen,
                     uint8_t *pntxt)
{
    uint8_t const *x = crtxt, *iv = iVec;
    uint8_t r = crtxtLen % BLOCKSIZE, *y;
    count_t n = crtxtLen / BLOCKSIZE;
#if CTS
    if (n == 0)
        return DECRYPTION_FAILURE;

    if (r == 0 && --n)
        r = BLOCKSIZE;
    n -= (r > 0) - !n; /*  last two blocks swapped  */
#else
    if (r)
        return DECRYPTION_FAILURE;
#endif

    AES_SetKey(key);
    for (y = pntxt; n--; y += BLOCKSIZE) {
        rijndaelDecrypt(x, y); /*  P = Dec(C) ^ IV          */
        xorBlock(iv, y);       /*  IV_next = C              */
        iv = x;
        x += BLOCKSIZE;
    } /*  r = 0 unless CTS enabled */
    if (r) { /*  P2 =  Dec(C1) ^ C2       */
        mixThenXor(&rijndaelDecrypt, x, y, x + BLOCKSIZE, r, y + BLOCKSIZE);
        memcpy(y, x + BLOCKSIZE, r);
        rijndaelDecrypt(y, y); /*  P1 = Dec(T) ^ IV, where  */
        xorBlock(iv, y);       /*  T = C2 padded by Dec(C1) */
    }
    BURN(RoundKey);

    /* note: if padding was applied, check whether output is properly padded. */
    return ENDED_IN_SUCCESS;
}
#endif /* CBC */


/*----------------------------------------------------------------------------*\
                      CFB-AES (cipher feedback) functions
\*----------------------------------------------------------------------------*/
#if IMPLEMENT(CFB)
/**
 * @brief   the general scheme of CFB-AES block-ciphering algorithm
 * @param   key       encryption key with a fixed size specified by KEYSIZE
 * @param   iVec      initialization vector
 * @param   mode      mode of operation: (1) to encrypt, (0) to decrypt
 * @param   input     buffer of the input plain/cipher-text
 * @param   dataSize  size of input in bytes
 * @param   output    buffer of the resulting cipher/plain-text
 */
static void CFB_Cipher(const uint8_t *key,
                       const block_t iVec,
                       const char mode,
                       const void *input,
                       const size_t dataSize,
                       void *output)
{
    uint8_t const *iv = iVec, *x;
    uint8_t *y = output, tmp[BLOCKSIZE];
    count_t n = dataSize / BLOCKSIZE; /*  number of full blocks    */

    AES_SetKey(key);
    for (x = input; n--; x += BLOCKSIZE) {
        rijndaelEncrypt(iv, y); /*  both in en[de]cryption:  */
        xorBlock(x, y);         /*  Y = Enc(IV) ^ X          */
        iv = mode ? y : x;      /*  IV_next = Ciphertext     */
        y += BLOCKSIZE;
    }
    mixThenXor(&rijndaelEncrypt, iv, tmp, x, dataSize % BLOCKSIZE, y);
    BURN(RoundKey);
}

/**
 * @brief   encrypt the input plaintext using CFB-AES block-cipher method
 * @param   key       encryption key with a fixed size specified by KEYSIZE
 * @param   iVec      initialization vector
 * @param   pntxt     input plaintext buffer
 * @param   ptextLen  size of plaintext in bytes
 * @param   crtxt     resulting cipher-text buffer
 */
void AES_CFB_encrypt(const uint8_t *key,
                     const block_t iVec,
                     const uint8_t *pntxt,
                     const size_t ptextLen,
                     uint8_t *crtxt)
{
    CFB_Cipher(key, iVec, 1, pntxt, ptextLen, crtxt);
}

/**
 * @brief   decrypt the input ciphertext using CFB-AES block-cipher method
 * @param   key       decryption key with a fixed size specified by KEYSIZE
 * @param   iVec      initialization vector
 * @param   crtxt     input ciphertext buffer
 * @param   crtxtLen  size of ciphertext in bytes
 * @param   pntxt     resulting plaintext buffer
 */
void AES_CFB_decrypt(const uint8_t *key,
                     const block_t iVec,
                     const uint8_t *crtxt,
                     const size_t crtxtLen,
                     uint8_t *pntxt)
{
    CFB_Cipher(key, iVec, 0, crtxt, crtxtLen, pntxt);
}
#endif /* CFB */


/*----------------------------------------------------------------------------*\
                      OFB-AES (output feedback) functions
\*----------------------------------------------------------------------------*/
#if IMPLEMENT(OFB)
/**
 * @brief   encrypt the input plaintext using OFB-AES block-cipher method
 * @param   key       encryption key with a fixed size specified by KEYSIZE
 * @param   iVec      initialization vector
 * @param   pntxt     input plaintext buffer
 * @param   ptextLen  size of plaintext in bytes
 * @param   crtxt     resulting cipher-text buffer
 */
void AES_OFB_encrypt(const uint8_t *key,
                     const block_t iVec,
                     const uint8_t *pntxt,
                     const size_t ptextLen,
                     uint8_t *crtxt)
{
    count_t n = ptextLen / BLOCKSIZE;
    uint8_t *y;
    block_t iv;

    memcpy(iv, iVec, sizeof iv);
    memcpy(crtxt, pntxt, ptextLen); /*  i.e. in-place encryption */

    AES_SetKey(key);
    for (y = crtxt; n--; y += BLOCKSIZE) {
        rijndaelEncrypt(iv, iv); /*  IV_next = Enc(IV)        */
        xorBlock(iv, y);         /*  C = IV_next ^ P          */
    }
    mixThenXor(&rijndaelEncrypt, iv, iv, y, ptextLen % BLOCKSIZE, y);
    BURN(RoundKey);
}

/**
 * @brief   decrypt the input ciphertext using OFB-AES block-cipher method
 * @param   key       decryption key with a fixed size specified by KEYSIZE
 * @param   iVec      initialization vector
 * @param   crtxt     input ciphertext buffer
 * @param   crtxtLen  size of ciphertext in bytes
 * @param   pntxt     resulting plaintext buffer
 */
void AES_OFB_decrypt(const uint8_t *key,
                     const block_t iVec,
                     const uint8_t *crtxt,
                     const size_t crtxtLen,
                     uint8_t *pntxt)
{
    AES_OFB_encrypt(key, iVec, crtxt, crtxtLen, pntxt);
}
#endif /* OFB */


/*----------------------------------------------------------------------------*\
    Parallelizable, counter-based modes of AES: demonstrating the main idea
               + How to use it in a simple, non-authenticated API
\*----------------------------------------------------------------------------*/
#if CTR
/**
 * @brief   the general scheme of operation in block-counter mode
 * @param   iCtr      initialized counter block
 * @param   big       big-endian block increment (1, 2) or little endian (0)
 * @param   input     buffer of the input plain/cipher-text
 * @param   dataSize  size of input in bytes
 * @param   output    buffer of the resulting cipher/plain-text
 */
static void CTR_Cipher(const block_t iCtr,
                       const char big,
                       const void *input,
                       const size_t dataSize,
                       void *output)
{
    block_t c, enc;
    count_t n = dataSize / BLOCKSIZE;
    uint8_t *y;

    memcpy(output, input, dataSize); /* do in-place en/decryption */
    memcpy(c, iCtr, sizeof c);
    if (big > 1)
        incBlock(c, 1); /* pre-increment for CCM/GCM */

    for (y = output; n--; y += BLOCKSIZE) {
        rijndaelEncrypt(c, enc); /*  both in en[de]cryption:  */
        xorBlock(enc, y);        /*  Y = Enc(Ctr) ^ X         */
        incBlock(c, big);        /*  Ctr_next = Ctr + 1       */
    }
    mixThenXor(&rijndaelEncrypt, c, c, y, dataSize % BLOCKSIZE, y);
}
#endif

#if IMPLEMENT(CTR_NA)
/**
 * @brief   encrypt the input plaintext using CTR-AES block-cipher method
 * @param   key       encryption key with a fixed size specified by KEYSIZE
 * @param   iv        initialization vector a.k.a. nonce
 * @param   pntxt     input plaintext buffer
 * @param   ptextLen  size of plaintext in bytes
 * @param   crtxt     resulting cipher-text buffer
 */
void AES_CTR_encrypt(const uint8_t *key,
                     const uint8_t *iv,
                     const uint8_t *pntxt,
                     const size_t ptextLen,
                     uint8_t *crtxt)
{
#if CTR_IV_LENGTH == BLOCKSIZE
#define CTRBLOCK iv
#else
    block_t CTRBLOCK = {0};
    memcpy(CTRBLOCK, iv, CTR_IV_LENGTH);
    xor2BVal(CTRBLOCK, CTR_STARTVALUE, LAST); /*  initialize the counter   */
#endif
    AES_SetKey(key);
    CTR_Cipher(CTRBLOCK, 1, pntxt, ptextLen, crtxt);
    BURN(RoundKey);
}

/**
 * @brief   decrypt the input ciphertext using CTR-AES block-cipher method
 * @param   key       decryption key with a fixed size specified by KEYSIZE
 * @param   iv        initialization vector a.k.a. nonce
 * @param   crtxt     input ciphertext buffer
 * @param   crtxtLen  size of ciphertext in bytes
 * @param   pntxt     resulting plaintext buffer
 */
void AES_CTR_decrypt(const uint8_t *key,
                     const uint8_t *iv,
                     const uint8_t *crtxt,
                     const size_t crtxtLen,
                     uint8_t *pntxt)
{
    AES_CTR_encrypt(key, iv, crtxt, crtxtLen, pntxt);
}
#endif /* CTR */


/*----------------------------------------------------------------------------*\
       XEX-AES based modes (xor-encrypt-xor): demonstrating the main idea
  + main functions of XTS-AES (XEX Tweaked-codebook with ciphertext Stealing)
\*----------------------------------------------------------------------------*/
#if IMPLEMENT(XTS)
/**
 * @brief   encrypt or decrypt a data unit with given key-pair using XEX method
 * @param   cipher    block cipher function: rijndaelEncrypt or rijndaelDecrypt
 * @param   keypair   pair of encryption keys, each one has KEYSIZE bytes
 * @param   tweak     data unit identifier block, similar to nonce in CTR mode
 * @param   sectid    sector id: if the given value is -1, use tweak value
 * @param   dataSize  size of input data, to be encrypted/decrypted
 * @param   T         one-time pad which is xor-ed with both plain/cipher text
 * @param   storage   working memory; result of encryption/decryption process
 */
static void XEX_Cipher(fmix_t cipher,
                       const uint8_t *keypair,
                       const block_t tweak,
                       const size_t sectid,
                       const size_t dataSize,
                       block_t T,
                       void *storage)
{
    uint8_t *y;
    count_t n = dataSize / BLOCKSIZE;

    if (sectid == ~(size_t) 0) {     /* the `i` block is either   */
        memcpy(T, tweak, BLOCKSIZE); /* ..a little-endian number  */
    } /* ..or a byte array.        */
    else {
        memset(T, 0, BLOCKSIZE);
        copyLVal(T, sectid, 0);
    }
    AES_SetKey(keypair + KEYSIZE); /* T = encrypt `i` with key2 */
    rijndaelEncrypt(T, T);

    AES_SetKey(keypair); /* key1 is set as cipher key */
    for (y = storage; n--; y += BLOCKSIZE) {
        xorBlock(T, y); /*  xor T with input         */
        cipher(y, y);
        xorBlock(T, y);  /*  Y = T ^ Cipher( T ^ X )  */
        doubleLGF128(T); /*  T_next = T * alpha       */
    }
}

/**
 * @brief   encrypt the input plaintext using XTS-AES block-cipher method
 * @param   keys      two-part encryption key with a fixed size of 2*KEYSIZE
 * @param   tweak     tweak bytes of data unit, a.k.a sector ID (little-endian)
 * @param   pntxt     input plaintext buffer
 * @param   ptextLen  size of plaintext in bytes
 * @param   crtxt     resulting cipher-text buffer
 */
char AES_XTS_encrypt(const uint8_t *keys,
                     const uint8_t *tweak,
                     const uint8_t *pntxt,
                     const size_t ptextLen,
                     uint8_t *crtxt)
{
    block_t T;
    uint8_t r = ptextLen % BLOCKSIZE, *c;
    size_t len = ptextLen - r;

    if (len == 0)
        return ENCRYPTION_FAILURE;
    memcpy(crtxt, pntxt, len); /* copy input data to output */

    XEX_Cipher(&rijndaelEncrypt, keys, tweak, ~0, len, T, crtxt);
    if (r) { /*  XTS for partial block    */
        c = crtxt + len - BLOCKSIZE;
        memcpy(crtxt + len, c, r); /* 'steal' the cipher-text   */
        memcpy(c, pntxt + len, r); /*  ..to fill the last chunk */
        xorBlock(T, c);
        rijndaelEncrypt(c, c);
        xorBlock(T, c);
    }
    BURN(RoundKey);
    return ENDED_IN_SUCCESS;
}

/**
 * @brief   encrypt the input ciphertext using XTS-AES block-cipher method
 * @param   keys      two-part encryption key with a fixed size of 2*KEYSIZE
 * @param   tweak     tweak bytes of data unit, a.k.a sector ID (little-endian)
 * @param   crtxt     input ciphertext buffer
 * @param   crtxtLen  size of ciphertext in bytes
 * @param   pntxt     resulting plaintext buffer
 */
char AES_XTS_decrypt(const uint8_t *keys,
                     const uint8_t *tweak,
                     const uint8_t *crtxt,
                     const size_t crtxtLen,
                     uint8_t *pntxt)
{
    block_t TT, T;
    uint8_t r = crtxtLen % BLOCKSIZE, *p;
    size_t len = crtxtLen - r;

    if (len == 0)
        return DECRYPTION_FAILURE;
    memcpy(pntxt, crtxt, len); /* copy input data to output */
    p = pntxt + len - BLOCKSIZE;

    XEX_Cipher(&rijndaelDecrypt, keys, tweak, ~0, len - BLOCKSIZE, T, pntxt);
    if (r) {
        memcpy(TT, T, sizeof T);
        doubleLGF128(TT);      /*  TT = T * alpha,          */
        xorBlock(TT, p);       /*  because the stolen       */
        rijndaelDecrypt(p, p); /*  ..ciphertext was xor-ed  */
        xorBlock(TT, p);       /*  ..with TT in encryption  */
        memcpy(pntxt + len, p, r);
        memcpy(p, crtxt + len, r);
    }
    xorBlock(T, p);
    rijndaelDecrypt(p, p);
    xorBlock(T, p);

    BURN(RoundKey);
    return ENDED_IN_SUCCESS;
}
#endif /* XTS */


/*----------------------------------------------------------------------------*\
       CMAC-AES (cipher-based message authentication code): main function
\*----------------------------------------------------------------------------*/
#if IMPLEMENT(CMAC)
/**
 * @brief   derive the AES-CMAC of input data using an encryption key
 * @param   key       AES encryption key
 * @param   data      buffer of input data
 * @param   dataSize  size of data in bytes
 * @param   mac       calculated CMAC hash
 */
void AES_CMAC(const uint8_t *key,
              const void *data,
              const size_t dataSize,
              block_t mac)
{
    block_t K1 = {0}, K2;
    memcpy(mac, K1, sizeof K1); /*  initialize mac           */
    getSubkeys(&doubleBGF128, 1, key, K1, K2);
    cMac(K1, K2, data, dataSize, mac);
    BURN(RoundKey);
}
#endif /* CMAC */


/*----------------------------------------------------------------------------*\
    GCM-AES (Galois counter mode): authentication with GMAC & main functions
\*----------------------------------------------------------------------------*/
#if IMPLEMENT(GCM)

/** calculate the GMAC of ciphertext and AAD using an authentication subkey H */
static void GHash(const block_t H,
                  const void *aData,
                  const void *crtxt,
                  const size_t adataLen,
                  const size_t crtxtLen,
                  block_t gsh)
{
    block_t len = {0};
    xor2BVal(len, adataLen * 8, LAST / 2);
    xor2BVal(len, crtxtLen * 8, LAST); /*  save bit-sizes into len  */

    xMac(aData, adataLen, H, &mulGF128, gsh); /*  first digest AAD, then   */
    xMac(crtxt, crtxtLen, H, &mulGF128, gsh); /*  ..ciphertext, and then   */
    xMac(len, sizeof len, H, &mulGF128, gsh); /*  ..bit sizes into GHash   */
}

/** encrypt zeros to get authentication subkey H, and prepare the IV for GCM. */
static void GCM_Init(const uint8_t *key,
                     const uint8_t *nonce,
                     block_t authKey,
                     block_t iv)
{
    AES_SetKey(key);
    rijndaelEncrypt(authKey, authKey); /* authKey = Enc(zero block) */
#if GCM_NONCE_LEN != 12
    GHash(authKey, NULL, nonce, 0, GCM_NONCE_LEN, iv);
#else
    memcpy(iv, nonce, 12);
    iv[LAST] = 1;
#endif
}

/**
 * @brief   encrypt the input plaintext using GCM-AES block-cipher method
 * @param   key       encryption key with a fixed size specified by KEYSIZE
 * @param   nonce     a.k.a initialization vector with fixed size: GCM_NONCE_LEN
 * @param   pntxt     input plain-text buffer
 * @param   ptextLen  size of plaintext in bytes
 * @param   aData     additional authentication data
 * @param   aDataLen  size of additional authentication data
 * @param   crtxt     resulting cipher-text buffer
 * @param   auTag     message authentication tag. buffer must be 16-bytes long
 */
void AES_GCM_encrypt(const uint8_t *key,
                     const uint8_t *nonce,
                     const uint8_t *pntxt,
                     const size_t ptextLen,
                     const uint8_t *aData,
                     const size_t aDataLen,
                     uint8_t *crtxt,
                     block_t auTag)
{
    block_t H = {0}, iv = {0}, gsh = {0};
    GCM_Init(key, nonce, H, iv); /*  get IV & auth. subkey H  */

    CTR_Cipher(iv, 2, pntxt, ptextLen, crtxt);
    rijndaelEncrypt(iv, auTag); /*  tag = Enc(iv) ^ GHASH    */
    BURN(RoundKey);
    GHash(H, aData, crtxt, aDataLen, ptextLen, gsh);
    xorBlock(gsh, auTag);
}

/**
 * @brief   decrypt the input ciphertext using GCM-AES block-cipher method
 * @param   key       decryption key with a fixed size specified by KEYSIZE
 * @param   nonce     a.k.a initialization vector with fixed size: GCM_NONCE_LEN
 * @param   crtxt     input cipher-text buffer + appended authentication tag
 * @param   crtxtLen  size of ciphertext, excluding tag
 * @param   aData     additional authentication data
 * @param   aDataLen  size of additional authentication data
 * @param   tagLen    length of authentication tag
 * @param   pntxt     resulting plaintext buffer
 * @return            whether message authentication was successful
 */
char AES_GCM_decrypt(const uint8_t *key,
                     const uint8_t *nonce,
                     const uint8_t *crtxt,
                     const size_t crtxtLen,
                     const uint8_t *aData,
                     const size_t aDataLen,
                     const uint8_t tagLen,
                     uint8_t *pntxt)
{
    block_t H = {0}, iv = {0}, gsh = {0};
    GCM_Init(key, nonce, H, iv);
    GHash(H, aData, crtxt, aDataLen, crtxtLen, gsh);

    rijndaelEncrypt(iv, H);
    xorBlock(H, gsh); /*   tag = Enc(iv) ^ GHASH   */
    if (MISMATCH(gsh, crtxt + crtxtLen, tagLen)) { /*  compare tags and */
        BURN(RoundKey); /*  ..proceed if they match  */
        return AUTHENTICATION_FAILURE;
    }
    CTR_Cipher(iv, 2, crtxt, crtxtLen, pntxt);
    BURN(RoundKey);
    return ENDED_IN_SUCCESS;
}
#endif /* GCM */


/*----------------------------------------------------------------------------*\
    CCM-AES (counter with CBC-MAC): CBC-MAC authentication & main functions
\*----------------------------------------------------------------------------*/
#if IMPLEMENT(CCM)

/** this function calculates the CBC-MAC of plaintext and authentication data */
static void CBCMac(const block_t iv,
                   const void *aData,
                   const void *pntxt,
                   const size_t aDataLen,
                   const size_t ptextLen,
                   block_t M)
{
    block_t A = {0};
    uint8_t p = 1, s = LAST - 1;
    memcpy(M, iv, BLOCKSIZE); /*  initialize CBC-MAC       */

    M[0] |= (CCM_TAG_LEN - 2) << 2; /*  set some flags on M_*    */
    xor2BVal(M, ptextLen, LAST);    /*  copy data size into M_*  */
    if (aDataLen)                   /*  feed aData into CBC-MAC  */
    {
        M[0] |= 0x40;
        rijndaelEncrypt(M, M); /*  flag M_* and encrypt it  */

        if (aDataLen < s)
            s = aDataLen;
        if (aDataLen > 0xFEFF) { /*  assuming aDataLen < 2^32 */
            p += 4;
            s -= 4;
            A[0] = 0xFF;
            A[1] = 0xFE; /*  prepend FFFE to aDataLen */
        }
        memcpy(A + p + 1, aData, s); /*  copy ADATA into A        */
        xor2BVal(A, aDataLen, p);    /*  prepend aDataLen         */
    }

    xMac(A, sizeof A, M, &rijndaelEncrypt, M); /*  CBC-MAC start of aData,  */
    if (aDataLen > s)                          /*  and then the rest of it  */
    {
        xMac((char const *) aData + s, aDataLen - s, M, &rijndaelEncrypt, M);
    }
    xMac(pntxt, ptextLen, M, &rijndaelEncrypt, M);
}

/**
 * @brief   encrypt the input plaintext using CCM-AES block-cipher method
 * @param   key       encryption key with a fixed size specified by KEYSIZE
 * @param   nonce     a.k.a initialization vector with fixed size: CCM_NONCE_LEN
 * @param   pntxt     input plain-text buffer
 * @param   ptextLen  size of plaintext in bytes
 * @param   aData     additional authentication data
 * @param   aDataLen  size of additional authentication data
 * @param   crtxt     resulting cipher-text buffer
 * @param   auTag     message authentication tag. buffer must be 16-bytes long
 */
void AES_CCM_encrypt(const uint8_t *key,
                     const uint8_t *nonce,
                     const uint8_t *pntxt,
                     const size_t ptextLen,
                     const uint8_t *aData,
                     const size_t aDataLen,
                     uint8_t *crtxt,
                     block_t auTag)
{
    block_t iv = {14 - CCM_NONCE_LEN, 0}, cbc;
    memcpy(iv + 1, nonce, CCM_NONCE_LEN);

    AES_SetKey(key);
    CBCMac(iv, aData, pntxt, aDataLen, ptextLen, cbc);
    CTR_Cipher(iv, 2, pntxt, ptextLen, crtxt);
    rijndaelEncrypt(iv, auTag);
    xorBlock(cbc, auTag); /*  tag = Enc(iv) ^ CBC-MAC  */
    BURN(RoundKey);
}

/**
 * @brief   decrypt the input ciphertext using CCM-AES block-cipher method
 * @param   key       decryption key with a fixed size specified by KEYSIZE
 * @param   nonce     a.k.a initialization vector with fixed size: CCM_NONCE_LEN
 * @param   crtxt     input cipher-text buffer + appended authentication tag
 * @param   crtxtLen  size of ciphertext, excluding tag
 * @param   aData     additional authentication data
 * @param   aDataLen  size of additional authentication data
 * @param   tagLen    length of authentication tag (if any)
 * @param   pntxt     resulting plaintext buffer
 * @return            whether message authentication was successful
 */
char AES_CCM_decrypt(const uint8_t *key,
                     const uint8_t *nonce,
                     const uint8_t *crtxt,
                     const size_t crtxtLen,
                     const uint8_t *aData,
                     const size_t aDataLen,
                     const uint8_t tagLen,
                     uint8_t *pntxt)
{
    block_t iv = {14 - CCM_NONCE_LEN, 0}, cbc;
    memcpy(iv + 1, nonce, CCM_NONCE_LEN);
    if (tagLen && tagLen != CCM_TAG_LEN)
        return DECRYPTION_FAILURE;

    AES_SetKey(key);
    CTR_Cipher(iv, 2, crtxt, crtxtLen, pntxt);
    CBCMac(iv, aData, pntxt, aDataLen, crtxtLen, cbc);
    rijndaelEncrypt(iv, iv); /*  tag = Enc(iv) ^ CBC-MAC  */
    BURN(RoundKey);

    xorBlock(iv, cbc); /*  verify the resulting tag */
    if (MISMATCH(cbc, crtxt + crtxtLen, tagLen)) {
        SABOTAGE(pntxt, crtxtLen);
        return AUTHENTICATION_FAILURE;
    }
    return ENDED_IN_SUCCESS;
}
#endif /* CCM */


/*----------------------------------------------------------------------------*\
       SIV-AES (synthetic init-vector): nonce synthesis & main functions
\*----------------------------------------------------------------------------*/
#if IMPLEMENT(SIV)

/** calculate the CMAC* of AAD unit(s), then plaintext, and synthesize the IV */
static void S2V(const uint8_t *key,
                const void *aData,
                const void *pntxt,
                const size_t aDataLen,
                const size_t ptextLen,
                block_t IV)
{
    block_t K[2], Y;
    uint8_t r = ptextLen % BLOCKSIZE, *D = K[0], *Q = K[1];

    memset(*K, 0, BLOCKSIZE);
    memset(IV, 0, BLOCKSIZE); /*  initialize/clear IV      */
    getSubkeys(&doubleBGF128, 1, key, D, Q);
    rijndaelEncrypt(D, Y); /*  Y_0 = CMAC(zero block)   */

    /* in case of multiple AAD units, each must be handled the same way as this.
     * e.g. let aData be a 2D array and aDataLen a null-terminated one. then the
     * following three lines starting with `if (aDataLen)` can be replaced by:
     * for (i = 0; *aDataLen; ++i) { cMac( D, Q, aData[i], *aDataLen++, IV ); */
    if (aDataLen) {
        cMac(D, Q, aData, aDataLen, IV);
        doubleBGF128(Y); /*  Y_$ = double( Y_{i-1} )  */
        xorBlock(IV, Y); /*  Y_i = Y_$ ^ CMAC(AAD_i)  */
        memset(IV, 0, BLOCKSIZE);
    }
    if (ptextLen < sizeof Y) { /*  for short messages:      */
        doubleBGF128(Y);       /*  Y = double( Y_n )        */
        r = 0;
    }
    if (r) {
        memset(D, 0, BLOCKSIZE);
    }
    xorBlock(Y, D + r);
    cMac(D, D, pntxt, ptextLen - r, IV); /*  CMAC*( Y  xor_end  M )   */
    if (r) {
        cMac(NULL, Q, (const char *) pntxt + ptextLen - r, r, IV);
    }
}

/**
 * @brief   encrypt the input plaintext using SIV-AES block-cipher method
 * @param   keys      two-part encryption key with a fixed size of 2*KEYSIZE
 * @param   pntxt     input plain-text buffer
 * @param   ptextLen  size of plaintext in bytes
 * @param   aData     additional authentication data
 * @param   aDataLen  size of additional authentication data
 * @param   iv        synthesized I.V block, typically prepended to ciphertext
 * @param   crtxt     resulting cipher-text buffer
 */
void AES_SIV_encrypt(const uint8_t *keys,
                     const uint8_t *pntxt,
                     const size_t ptextLen,
                     const uint8_t *aData,
                     const size_t aDataLen,
                     block_t iv,
                     uint8_t *crtxt)
{
    block_t IV;
    S2V(keys, aData, pntxt, aDataLen, ptextLen, IV);
    memcpy(iv, IV, sizeof IV);
    IV[8] &= 0x7F;
    IV[12] &= 0x7F; /*  clear 2 bits for cipher  */

    AES_SetKey(keys + KEYSIZE);
    CTR_Cipher(IV, 1, pntxt, ptextLen, crtxt);
    BURN(RoundKey);
}

/**
 * @brief   decrypt the input ciphertext using SIV-AES block-cipher method
 * @param   keys      two-part encryption key with a fixed size of 2*KEYSIZE
 * @param   iv        provided I.V block to validate
 * @param   crtxt     input cipher-text buffer
 * @param   crtxtLen  size of ciphertext in bytes
 * @param   aData     additional authentication data
 * @param   aDataLen  size of additional authentication data
 * @param   pntxt     resulting plaintext buffer
 * @return            whether synthesized I.V. matched the provided one
 */
char AES_SIV_decrypt(const uint8_t *keys,
                     const block_t iv,
                     const uint8_t *crtxt,
                     const size_t crtxtLen,
                     const uint8_t *aData,
                     const size_t aDataLen,
                     uint8_t *pntxt)
{
    block_t IV;
    memcpy(IV, iv, sizeof IV);
    IV[8] &= 0x7F;
    IV[12] &= 0x7F; /*  clear two bits           */

    AES_SetKey(keys + KEYSIZE);
    CTR_Cipher(IV, 1, crtxt, crtxtLen, pntxt);
    S2V(keys, aData, pntxt, aDataLen, crtxtLen, IV);
    BURN(RoundKey);

    if (MISMATCH(IV, iv, sizeof IV)) /* verify the synthesized IV */
    {
        SABOTAGE(pntxt, crtxtLen);
        return AUTHENTICATION_FAILURE;
    }
    return ENDED_IN_SUCCESS;
}
#endif /* SIV */


/*----------------------------------------------------------------------------*\
      SIV-GCM-AES (Galois counter mode with synthetic i.v): main functions
\*----------------------------------------------------------------------------*/
#if IMPLEMENT(GCM_SIV)

/** calculates the POLYVAL of plaintext and AAD using authentication subkey H */
static void Polyval(const block_t H,
                    const void *aData,
                    const void *pntxt,
                    const size_t aDataLen,
                    const size_t ptextLen,
                    block_t pv)
{
    block_t len = {0}; /*  save bit-sizes into len  */
    copyLVal(len, aDataLen * 8, 0);
    copyLVal(len, ptextLen * 8, 8);

    xMac(aData, aDataLen, H, &dotGF128, pv); /*  first digest AAD, then   */
    xMac(pntxt, ptextLen, H, &dotGF128, pv); /*  ..plaintext, and then    */
    xMac(len, sizeof len, H, &dotGF128, pv); /*  ..bit sizes into POLYVAL */
}

/** derive the pair of authentication-encryption-keys from main key and nonce */
static void GCMSIV_Init(const uint8_t *key, const uint8_t *nonce, block_t AK)
{
    uint8_t iv[10 * Nb + KEYSIZE], *h, *k;
    k = h = iv + BLOCKSIZE;
    memcpy(iv + 4, nonce, 12);

    AES_SetKey(key);
    for (*(int32_t *) iv = 0; *iv < 2 + Nk / 2; ++*iv) {
        rijndaelEncrypt(iv, k); /* encrypt & take half, then */
        k += 8;                 /* ..increment iv's LSB      */
    }
    AES_SetKey(k - KEYSIZE);  /*  set the main cipher-key  */
    memcpy(AK, h, BLOCKSIZE); /*  take authentication key  */
}

/**
 * @brief   encrypt the input plaintext using SIV-GCM-AES block-cipher method
 * @param   key       encryption key with a fixed size specified by KEYSIZE
 * @param   nonce     provided 96-bit nonce
 * @param   pntxt     input plain-text buffer
 * @param   ptextLen  size of plaintext in bytes
 * @param   aData     additional authentication data
 * @param   aDataLen  size of additional authentication data
 * @param   crtxt     resulting cipher-text buffer,
 * @param   auTag     appended authentication tag. must be 16-bytes long
 */
void GCM_SIV_encrypt(const uint8_t *key,
                     const uint8_t *nonce,
                     const uint8_t *pntxt,
                     const size_t ptextLen,
                     const uint8_t *aData,
                     const size_t aDataLen,
                     uint8_t *crtxt,
                     block_t auTag)
{
    block_t H, S = {0};
    GCMSIV_Init(key, nonce, H); /* get authentication subkey */

    Polyval(H, aData, pntxt, aDataLen, ptextLen, S);
    XOR32BITS(nonce[0], S[0]);
    XOR32BITS(nonce[4], S[4]);
    XOR32BITS(nonce[8], S[8]); /* xor POLYVAL with nonce    */

    S[LAST] &= 0x7F;       /* clear one bit & encrypt,  */
    rijndaelEncrypt(S, S); /* ..to get auth. tag        */
    memcpy(auTag, S, sizeof S);

    S[LAST] |= 0x80; /* set 1 bit to get CTR's IV */
    CTR_Cipher(S, 0, pntxt, ptextLen, crtxt);
    BURN(RoundKey);
}

/**
 * @brief   decrypt the input ciphertext using SIV-GCM-AES block-cipher method
 * @param   key       decryption key with a fixed size specified by KEYSIZE
 * @param   nonce     provided 96-bit nonce
 * @param   crtxt     input cipher-text buffer + appended authentication tag
 * @param   crtxtLen  size of ciphertext, excluding tag
 * @param   aData     additional authentication data
 * @param   aDataLen  size of additional authentication data
 * @param   tagLen    length of authentication tag; MUST be 16 bytes
 * @param   pntxt     resulting plaintext buffer
 * @return            whether message authentication/decryption was successful
 */
char GCM_SIV_decrypt(const uint8_t *key,
                     const uint8_t *nonce,
                     const uint8_t *crtxt,
                     const size_t crtxtLen,
                     const uint8_t *aData,
                     const size_t aDataLen,
                     const uint8_t tagLen,
                     uint8_t *pntxt)
{
    block_t H, S;
    if (tagLen != sizeof S)
        return DECRYPTION_FAILURE;

    GCMSIV_Init(key, nonce, H); /* get authentication subkey */
    memcpy(S, crtxt + crtxtLen, tagLen);
    S[LAST] |= 0x80; /* tag is IV for CTR cipher  */
    CTR_Cipher(S, 0, crtxt, crtxtLen, pntxt);

    memset(S, 0, sizeof S);
    Polyval(H, aData, pntxt, aDataLen, crtxtLen, S);
    XOR32BITS(nonce[0], S[0]);
    XOR32BITS(nonce[4], S[4]);
    XOR32BITS(nonce[8], S[8]); /* xor POLYVAL with nonce    */

    S[LAST] &= 0x7F;       /* clear one bit & encrypt,  */
    rijndaelEncrypt(S, S); /* ..to get tag & verify it  */
    BURN(RoundKey);
    if (MISMATCH(S, crtxt + crtxtLen,
                 sizeof S)) { /*  tag verification failed  */
        SABOTAGE(pntxt, crtxtLen);
        return AUTHENTICATION_FAILURE;
    }
    return ENDED_IN_SUCCESS;
}
#endif /* GCM-SIV */


/*----------------------------------------------------------------------------*\
   EAX-AES (encrypt-then-authenticate-then-translate): OMAC & main functions
\*----------------------------------------------------------------------------*/
#if IMPLEMENT(EAX)

/** this function calculates the OMAC of a data array using D (K1) and Q (K2) */
static void OMac(const uint8_t t,
                 const block_t D,
                 const block_t Q,
                 const void *data,
                 const size_t dataSize,
                 block_t mac)
{
    memset(mac, 0, BLOCKSIZE);
#if EAXP
    if (dataSize == 0 && t)
        return; /*  ignore null ciphertext   */

    memcpy(mac, t ? Q : D, BLOCKSIZE);
#else
    if (dataSize == 0)
        memcpy(mac, D, BLOCKSIZE);

    mac[LAST] ^= t;
    rijndaelEncrypt(mac, mac);
    if (dataSize == 0)
        return; /*  OMAC = CMAC( [t]_n )     */
#endif
    cMac(D, Q, data, dataSize, mac);
}

/**
 * @brief   encrypt the input plaintext using EAX-AES block-cipher method
 * @param   key       encryption key with a fixed size specified by KEYSIZE
 * @param   nonce     a.k.a init-vector with EAX_NONCE_LEN bytes unless EAX'
 * @param   pntxt     input plain-text buffer
 * @param   ptextLen  size of plaintext in bytes
 * @param   nonceLen  size of the nonce byte array; should be non-zero in EAX'
 * @param   aData     additional authentication data; for EAX only, not EAX'
 * @param   aDataLen  size of additional authentication data
 * @param   crtxt     resulting cipher-text buffer; 4 bytes mac appended in EAX'
 * @param   auTag     authentication tag; buffer must be 16 bytes long in EAX
 */
void AES_EAX_encrypt(const uint8_t *key,
                     const uint8_t *nonce,
                     const uint8_t *pntxt,
                     const size_t ptextLen,
#if EAXP
                     const size_t nonceLen,
                     uint8_t *crtxt)
#define F_DOUBLE doubleLGF128
#else
                     const uint8_t *aData,
                     const size_t aDataLen,
                     uint8_t *crtxt,
                     block_t auTag)
#define F_DOUBLE doubleBGF128
#define nonceLen EAX_NONCE_LEN
#endif
{
    block_t D = {0}, Q, mac;
    getSubkeys(&F_DOUBLE, 1, key, D, Q);
    OMac(0, D, Q, nonce, nonceLen, mac); /*  N = OMAC(0; nonce)       */

#if EAXP
    COPY32BIT(mac[12], crtxt[ptextLen]);
    mac[12] &= 0x7F;
    mac[14] &= 0x7F; /*  clear 2 bits to get N'   */
    CTR_Cipher(mac, 1, pntxt, ptextLen, crtxt);

    OMac(2, D, Q, crtxt, ptextLen, mac); /*  C' = CMAC'( ciphertext ) */
    XOR32BITS(mac[12], crtxt[ptextLen]); /*  tag (i.e mac) = N ^ C'   */
#else
    OMac(1, D, Q, aData, aDataLen, auTag); /*  H = OMAC(1; adata)       */
    xorBlock(mac, auTag);
    CTR_Cipher(mac, 1, pntxt, ptextLen, crtxt);

    OMac(2, D, Q, crtxt, ptextLen, mac); /*  C = OMAC(2; ciphertext)  */
    xorBlock(mac, auTag);                /*  tag = N ^ H ^ C          */
#endif
    BURN(RoundKey);
}

/**
 * @brief   decrypt the input ciphertext using EAX-AES block-cipher method
 * @param   key       decryption key with a fixed size specified by KEYSIZE
 * @param   nonce     a.k.a init-vector with EAX_NONCE_LEN bytes unless EAX'
 * @param   crtxt     input cipher-text buffer + appended authentication tag
 * @param   crtxtLen  size of cipher-text; excluding tag / 4-bytes mac in EAX'
 * @param   nonceLen  size of the nonce byte array; should be non-zero in EAX'
 * @param   aData     additional authentication data; for EAX only, not EAX'
 * @param   aDataLen  size of additional authentication data
 * @param   tagLen    length of authentication tag; mandatory 4 bytes in EAX'
 * @param   pntxt     resulting plaintext buffer
 * @return            whether message authentication was successful
 */
char AES_EAX_decrypt(const uint8_t *key,
                     const uint8_t *nonce,
                     const uint8_t *crtxt,
                     const size_t crtxtLen,
#if EAXP
                     const size_t nonceLen,
#else
                     const uint8_t *aData,
                     const size_t aDataLen,
                     const uint8_t tagLen,
#endif
                     uint8_t *pntxt)
{
    block_t D = {0}, Q, mac, tag;
    getSubkeys(&F_DOUBLE, 1, key, D, Q);
    OMac(2, D, Q, crtxt, crtxtLen, tag); /*  C = OMAC(2; ciphertext)  */

#if EAXP
    OMac(0, D, Q, nonce, nonceLen, mac); /*  N = CMAC'( nonce )       */
    XOR32BITS(crtxt[crtxtLen], tag[12]);
    XOR32BITS(mac[12], tag[12]);
    mac[12] &= 0x7F;
    mac[14] &= 0x7F; /*  clear 2 bits to get N'   */

    if (0 != *(int32_t *) &tag[12]) /*  result of mac validation */
#else
    OMac(1, D, Q, aData, aDataLen, mac); /*  H = OMAC(1; adata)       */
    xorBlock(mac, tag);
    OMac(0, D, Q, nonce, nonceLen, mac); /*  N = OMAC(0; nonce)       */
    xorBlock(mac, tag);                  /*  tag = N ^ H ^ C          */

    if (MISMATCH(tag, crtxt + crtxtLen, tagLen))
#endif
    { /* authenticate then decrypt */
        BURN(RoundKey);
        return AUTHENTICATION_FAILURE;
    }
    CTR_Cipher(mac, 1, crtxt, crtxtLen, pntxt);

    BURN(RoundKey);
    return ENDED_IN_SUCCESS;
}
#endif /* EAX */


/*----------------------------------------------------------------------------*\
        OCB-AES (offset codebook mode): how to parallelize the algorithm
                by independent calculation of the offset values
                 + auxiliary functions along with the main API
\*----------------------------------------------------------------------------*/
#if IMPLEMENT(OCB)

static block_t OCBsubkeys[4]; /*  [L_$] [L_*] [Ktop] [Δ_n] */

/** Calculate the offset block (Δ_i) at a specified index, given the initial Δ_0
 * and L$ blocks. This method has minimum memory usage, but it's clearly slow */
static void getDelta(const count_t index, block_t delta)
{
    size_t m, b = 1;
    block_t L;
    memcpy(L, *OCBsubkeys, sizeof L); /*  initialize L_$           */

    while (b <= index && b) /*  we can pre-calculate all */
    {                       /*  .. L_{i}s to boost speed */
        m = (4 * b - 1) & (index - b);
        b <<= 1;         /*  L_0 = double( L_$ )      */
        doubleBGF128(L); /*  L_i = double( L_{i-1} )  */
        if (b > m)
            xorBlock(L, delta); /*  Δ_i = Δ_{i-1} ^ L_ntz(i) */
    }
}

/** encrypt or decrypt the input data with OCB method. cipher function is either
 * rijndaelEncrypt or rijndaelDecrypt, and nonce size must be = OCB_NONCE_LEN */
static void OCB_Cipher(fmix_t cipher,
                       const uint8_t *nonce,
                       const size_t dataSize,
                       void *data)
{
    uint8_t *Ls = OCBsubkeys[1], *kt = OCBsubkeys[2], *del = OCBsubkeys[3], *y;
    count_t i = 0, n = nonce[OCB_NONCE_LEN - 1] % 64;
    uint8_t j = 0, r = n % 8; /*  n = last 6 bits of nonce */

    memcpy(kt + BLOCKSIZE - OCB_NONCE_LEN, nonce, OCB_NONCE_LEN);
    kt[0] = OCB_TAG_LEN << 4 & 0xFF;
    kt[LAST - OCB_NONCE_LEN] |= 1;
    kt[LAST] &= 0xC0; /*  clear last 6 bits        */

    rijndaelEncrypt(kt, kt); /*  construct K_top          */
    memcpy(del, kt + 1, 8);  /*  stretch K_top            */
    xorBlock(kt, del);

    for (n /= 8; j < BLOCKSIZE; ++n) /* shift the stretched K_top */
    {
        kt[j++] = kt[n] << r | kt[n + 1] >> (8 - r);
    }
    if ((n = dataSize / BLOCKSIZE) == 0) {
        memcpy(del, kt, BLOCKSIZE); /*  initialize Δ_0           */
    }

    for (y = data; i++ < n; y += BLOCKSIZE) {
        memcpy(del, kt, BLOCKSIZE);
        getDelta(i, del); /*  calculate Δ_i using my   */
        xorBlock(del, y); /*  .. 'magic' algorithm     */
        cipher(y, y);
        xorBlock(del, y); /* Y = Δ_i ^ Cipher(Δ_i ^ X) */
    }
    if ((r = dataSize % BLOCKSIZE) != 0) { /*  Y_* = Enc(L_* ^ Δ_n) ^ X */
        xorBlock(Ls, del);
        mixThenXor(&rijndaelEncrypt, del, kt, y, r, y);
        del[r] ^= 0x80; /*        pad Δ_* (i.e. tag) */
    }
}

static void nop(const block_t x, block_t y) {}

/** calculate tag and save to Δ_*, using plaintext checksum and PMAC of aData */
static void OCB_GetTag(const void *pntxt,
                       const void *aData,
                       const size_t ptextLen,
                       const size_t aDataLen)
{
    block_t P = {0};
    count_t n = aDataLen / BLOCKSIZE;
    uint8_t r = aDataLen % BLOCKSIZE, *tag = OCBsubkeys[3];
    uint8_t const *Ld = OCBsubkeys[0], *Ls = OCBsubkeys[1];
    uint8_t const *xa = (uint8_t const *) aData + aDataLen - r;

    xMac(pntxt, ptextLen, NULL, &nop, tag); /*  get plaintext checksum   */
    xorBlock(Ld, tag);                      /*  S = Δ_* ^ checksum ^ L_$ */
    rijndaelEncrypt(tag, tag);              /*  Tag_0 = Enc( S )         */

    if (r) /*  PMAC authentication:     */
    {
        getDelta(n, P);
        cMac(NULL, Ls, xa, r, P); /*  P = Enc(A_* ^ L_* ^ Δ_n) */
        xorBlock(P, tag);         /*  add it to tag            */
    }
    for (xa -= BLOCKSIZE; n; xa -= BLOCKSIZE) {
        memcpy(P, xa, sizeof P); /*  initialize Δ             */
        getDelta(n--, P);
        rijndaelEncrypt(P, P); /*  P_i = Enc(A_i ^ Δ_i)     */
        xorBlock(P, tag);      /*  add P_i to tag           */
    }
}

/**
 * @brief   encrypt the input stream using OCB-AES block-cipher method
 * @param   key       encryption key with a fixed size specified by KEYSIZE
 * @param   nonce     a.k.a initialization vector with fixed size: OCB_NONCE_LEN
 * @param   pntxt     input plain-text buffer
 * @param   ptextLen  size of plaintext in bytes
 * @param   aData     additional authentication data
 * @param   aDataLen  size of additional authentication data
 * @param   crtxt     resulting cipher-text buffer
 * @param   auTag     message authentication tag. buffer must be 16-bytes long
 */
void AES_OCB_encrypt(const uint8_t *key,
                     const uint8_t *nonce,
                     const uint8_t *pntxt,
                     const size_t ptextLen,
                     const uint8_t *aData,
                     const size_t aDataLen,
                     uint8_t *crtxt,
                     block_t auTag)
{
    uint8_t *Ld = OCBsubkeys[0], *Ls = OCBsubkeys[1], *tag = OCBsubkeys[3];

    memcpy(crtxt, pntxt, ptextLen); /* doing in-place encryption */
    memset(Ls, 0, 2 * BLOCKSIZE);
    getSubkeys(&doubleBGF128, 0, key, Ls, Ld);
    OCB_Cipher(&rijndaelEncrypt, nonce, ptextLen, crtxt);
    OCB_GetTag(pntxt, aData, ptextLen, aDataLen);

    memcpy(auTag, tag, OCB_TAG_LEN);
    BURN(RoundKey);
}

/**
 * @brief   decrypt the input stream using OCB-AES block-cipher method
 * @param   key       decryption key with a fixed size specified by KEYSIZE
 * @param   nonce     a.k.a initialization vector with fixed size: OCB_NONCE_LEN
 * @param   crtxt     input cipher-text buffer + appended authentication tag
 * @param   crtxtLen  size of ciphertext, excluding tag
 * @param   aData     additional authentication data
 * @param   aDataLen  size of additional authentication data
 * @param   tagLen    length of authentication tag (if any)
 * @param   pntxt     resulting plaintext buffer
 * @return            whether message authentication was successful
 */
char AES_OCB_decrypt(const uint8_t *key,
                     const uint8_t *nonce,
                     const uint8_t *crtxt,
                     const size_t crtxtLen,
                     const uint8_t *aData,
                     const size_t aDataLen,
                     const uint8_t tagLen,
                     uint8_t *pntxt)
{
    uint8_t *Ld = OCBsubkeys[0], *Ls = OCBsubkeys[1], *tag = OCBsubkeys[3];
    if (tagLen && tagLen != OCB_TAG_LEN)
        return DECRYPTION_FAILURE;

    memcpy(pntxt, crtxt, crtxtLen); /* in-place decryption       */
    memset(Ls, 0, 2 * BLOCKSIZE);
    getSubkeys(&doubleBGF128, 0, key, Ls, Ld);
    OCB_Cipher(&rijndaelDecrypt, nonce, crtxtLen, pntxt);
    OCB_GetTag(pntxt, aData, crtxtLen, aDataLen);

    BURN(RoundKey);
    if (MISMATCH(tag, crtxt + crtxtLen, tagLen)) {
        SABOTAGE(pntxt, crtxtLen);
        BURN(OCBsubkeys);
        return AUTHENTICATION_FAILURE;
    }
    return ENDED_IN_SUCCESS;
}
#endif /* OCB */


/*----------------------------------------------------------------------------*\
             KW-AES: Main functions for AES key-wrapping (RFC-3394)
\*----------------------------------------------------------------------------*/
#if IMPLEMENT(KWA)
#define HB (BLOCKSIZE / 2) /*  size of half-blocks      */
/**
 * @brief   wrap the input secret whose size is a multiple of 8 and >= 16
 * @param   kek       key-encryption-key a.k.a master key
 * @param   secret    input plain text secret
 * @param   secretLen size of input, must be a multiple of HB (half-block size)
 * @param   wrapped   wrapped secret, prepended with an additional half-block
 * @return            error if size is not a multiple of HB, or size < BLOCKSIZE
 */
char AES_KEY_wrap(const uint8_t *kek,
                  const uint8_t *secret,
                  const size_t secretLen,
                  uint8_t *wrapped)
{
    size_t n = secretLen / HB, i = 0, q; /*  number of semi-blocks    */
    block_t A;
    if (n < 2 || secretLen % HB)
        return ENCRYPTION_FAILURE;

    memset(A, 0xA6, HB);                     /*  initialization vector    */
    memcpy(wrapped + HB, secret, secretLen); /*  copy input to the output */
    AES_SetKey(kek);

    for (q = 6 * n; i < q;) {
        uint8_t *r = wrapped + (i++ % n + 1) * HB;
        memcpy(A + HB, r, HB);
        rijndaelEncrypt(A, A);  /*  A = Enc( V | R_{k-1} )   */
        memcpy(r, A + HB, HB);  /*  R_{k} = LSB(64, A)       */
        xor2BVal(A, i, HB - 1); /*  V = MSB(64, A) ^ i       */
    }
    BURN(RoundKey);

    memcpy(wrapped, A, HB);
    return ENDED_IN_SUCCESS;
}

/**
 * @brief   unwrap a wrapped input key whose size is a multiple of 8 and >= 24
 * @param   kek       key-encryption-key a.k.a master key
 * @param   wrapped   cipher-text input, i.e. wrapped secret.
 * @param   wrapLen   size of ciphertext/wrapped input in bytes
 * @param   secret    unwrapped secret whose size = wrapLen - HB
 * @return            a value indicating whether decryption was successful
 */
char AES_KEY_unwrap(const uint8_t *kek,
                    const uint8_t *wrapped,
                    const size_t wrapLen,
                    uint8_t *secret)
{
    size_t i, n = wrapLen / HB; /*  number of semi-blocks    */
    block_t A;
    if (n-- < 3 || wrapLen % HB)
        return DECRYPTION_FAILURE;

    memcpy(A, wrapped, HB); /*  authentication vector    */
    memcpy(secret, wrapped + HB, wrapLen - HB);
    AES_SetKey(kek);

    for (i = 6 * n; i; --i) {
        uint8_t *r = secret + ((i - 1) % n) * HB;
        xor2BVal(A, i, HB - 1);
        memcpy(A + HB, r, HB); /*  V = MSB(64, A) ^ i       */
        rijndaelDecrypt(A, A); /*  A = Dec( V | R_{k} )     */
        memcpy(r, A + HB, HB); /*  R_{k-1} = LSB(64, A)     */
    }
    BURN(RoundKey);

    for (n = 0; i < HB; ++i)
        n |= A[i] ^ 0xA6; /*  authenticate/error check */

    return n ? AUTHENTICATION_FAILURE : ENDED_IN_SUCCESS;
}
#endif /* KWA */


/*----------------------------------------------------------------------------*\
     Poly1305-AES message authentication: auxiliary functions and main API
\*----------------------------------------------------------------------------*/
#if IMPLEMENT(POLY1305)
#define SP 17 /* size of poly1305 blocks   */

/** derive modulo(2^130-5) for a little endian block, by repeated subtraction */
static void modP1305(uint8_t *block, const int ovrfl)
{
    int32_t q = ovrfl << 6 | block[SP - 1] / 4, t;
    uint8_t i = 0;
    if (!q)
        return; /*   q = B / (2 ^ 130)       */

    for (t = 5 * q; t && i < SP; t >>= 8) /* mod = B - q * (2^130-5)   */
    {
        t += block[i];            /* to get mod, first derive  */
        block[i++] = (uint8_t) t; /* .. B + (5 * q) and then   */
    } /* .. subtract q * (2^130)   */
    block[SP - 1] -= 4 * (uint8_t) q;
}

/** add two little-endian poly1305 blocks. use modular addition if necessary. */
static void addLBlocks(const uint8_t *x, const uint8_t len, uint8_t *y)
{
    int a, i;
    for (i = a = 0; i < len; a >>= 8) {
        a += x[i] + y[i];
        y[i++] = (uint8_t) a; /*  a >> 8 is overflow/carry */
    }
    if (i == SP)
        modP1305(y, a);
}

/** modular multiplication of two little-endian poly1305 blocks: y *= x mod P */
static void mulLBlocks(const uint8_t *x, uint8_t *y)
{
    uint8_t i, sh, n = SP, prod[SP] = {0}; /*  Y = [Y_0][Y_1]...[Y_n]   */
    int32_t m;
    while (n--)                          /* multiply X by MSB of Y    */
    {                                    /* ..and add to the result   */
        sh = n ? 8 : 0;                  /* ..shift the result if Y   */
        for (m = i = 0; i < SP; m >>= 8) /* ..has other byte in queue */
        {                                /* ..but don't shift for Y_0 */
            m += (prod[i] + x[i] * y[n]) << sh;
            prod[i++] = (uint8_t) m;
        }
        modP1305(prod, m); /*  modular multiplication   */
    }
    memcpy(y, prod, SP);
}

/** handle some special/rare cases that might be missed by modP1305 function. */
static void cmpToP1305(uint8_t *block)
{
    uint8_t n = (block[SP - 1] == 3) * (SP - 1);
    int c = block[SP - 1] > 3 || (n && block[0] >= 0xFB);

    while (c && n)
        c = (block[--n] == 0xFF); /* compare block to 2^130-5, */

    for (c *= 5; c; c >>= 8) { /* and if (block >= 2^130-5) */
        c += *block;           /* .. add it with 5          */
        *block++ = (uint8_t) c;
    }
}

/**
 * @brief   derive the Poly1305-AES mac of message using a nonce and key pair.
 * @param   keys      pair of encryption/mixing keys (k, r); size = KEYSIZE + 16
 * @param   nonce     a 128 bit string which is encrypted by AES_k
 * @param   data      buffer of input data
 * @param   dataSize  size of data in bytes
 * @param   mac       calculated Poly1305-AES mac
 */
void AES_Poly1305(const uint8_t *keys,
                  const block_t nonce,
                  const void *data,
                  const size_t dataSize,
                  block_t mac)
{
    uint8_t r[SP], rk[SP] = {1}, c[SP] = {0}, poly[SP] = {0};
    uint8_t n = (dataSize > 0);
    uint8_t s = (dataSize - n) % BLOCKSIZE + n; /* size of last chunk        */
    count_t q = (dataSize - n) / BLOCKSIZE + n;
    const char *pos = (const char *) data + dataSize;

    AES_SetKey(keys);
    rijndaelEncrypt(nonce, mac); /* derive AES_k(nonce)       */
    BURN(RoundKey);

    memcpy(r, keys + KEYSIZE, n = SP - 1); /* extract r from (k,r) pair */
    for (r[n] = 0; n; n -= 4) {
        r[n] &= 0xFC;     /* clear bottom 2 bits       */
        r[n - 1] &= 0x0F; /* clear top 4 bits          */
    }

    for (pos -= s; q--; pos -= (s = BLOCKSIZE)) {
        memcpy(c, pos, s);             /* copy message to chunk     */
        c[s] = 1;                      /* append 1 to each chunk    */
        mulLBlocks(r, rk);             /* r^k = r^{k-1} * r         */
        mulLBlocks(rk, c);             /* calculate c_{q-k} * r^k   */
        addLBlocks(c, sizeof c, poly); /* add to poly (mod 2^130-5) */
    }
    cmpToP1305(poly);
    addLBlocks(poly, BLOCKSIZE, mac); /* mac = poly + AES_k(nonce) */
}
#endif /* POLY1305 */


/*----------------------------------------------------------------------------*\
   FPE-AES (format-preserving encryption): definitions & auxiliary functions
\*----------------------------------------------------------------------------*/
#if IMPLEMENT(FPE)

#if CUSTOM_ALPHABET
/*
 * If your desired alphabet contains non-ASCII characters, the CUSTOM_ALPHABET
 * macro 'must be' set to a double-digit number, e.g 21. In what follows, there
 * are some sample alphabets along with their corresponding macro definitions.
 * It is straightforward to define another alphabet according to these samples.
 */
#define NON_ASCII_CHARACTER_SET (CUSTOM_ALPHABET >= 10)

/*
 * These strings are commonly used in ASCII-based alphabets. The declaration of
 * an alphabet must be followed by its number of characters (RADIX).
 */
#define DECDIGIT "0123456789"
#define LCLETTER "abcdefghijklmnopqrstuvwxyz"
#define UCLETTER "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
#define HEXDIGIT DECDIGIT "ABCDEFabcdef"

/**
 numbers
 */
#if CUSTOM_ALPHABET == 0
#define ALPHABET DECDIGIT
#define RADIX 10
#endif

/**
 binary numbers
 */
#if CUSTOM_ALPHABET == 1
#define ALPHABET "01"
#define RADIX 2
#endif

/**
 lowercase english words
 */
#if CUSTOM_ALPHABET == 2
#define ALPHABET LCLETTER
#define RADIX 26
#endif

/**
 lowercase alphanumeric strings
 */
#if CUSTOM_ALPHABET == 3
#define ALPHABET DECDIGIT LCLETTER
#define RADIX 36
#endif

/**
 the English alphabet
 */
#if CUSTOM_ALPHABET == 4
#define ALPHABET UCLETTER LCLETTER
#define RADIX 52
#endif

/**
 base-64 encoded strings (RFC-4648), with no padding character
 */
#if CUSTOM_ALPHABET == 5
#define ALPHABET UCLETTER LCLETTER DECDIGIT "+/"
#define RADIX 64
#endif

/**
 base-85 encoded strings (RFC-1924)
 */
#if CUSTOM_ALPHABET == 6
#define ALPHABET DECDIGIT UCLETTER LCLETTER "!#$%&()*+-;<=>?@^_`{|}~"
#define RADIX 85
#endif

/**
 a character set with length 26, used by some test vectors
 */
#if CUSTOM_ALPHABET == 7
#define ALPHABET DECDIGIT "abcdefghijklmnop"
#define RADIX 26
#endif

/**
 base-64 character set with DIFFERENT ORDERING, used by some test vectors
 */
#if CUSTOM_ALPHABET == 8
#define ALPHABET DECDIGIT UCLETTER LCLETTER "+/"
#define RADIX 64
#endif

/**
 all printable ascii characters
 */
#if CUSTOM_ALPHABET == 9
#define ALPHABET \
    " !\"#$%&\'()*+,-./" DECDIGIT ":;<=>?@" UCLETTER "[\\]^_`" LCLETTER "{|}~"
#define RADIX 95
#endif

/*
 * Here goes non-ASCII alphabets. Note that C89/ANSI-C standard does not fully
 * support such characters, and the code may lose its compliance in this case.
 */
#if NON_ASCII_CHARACTER_SET
#include <locale.h>

#include <wchar.h>
#define string_t wchar_t * /*    string pointer type    */
#else
#define string_t char *
#endif

/**
 Greek alphabet (LTR)
 */
#if CUSTOM_ALPHABET == 10
#define ALPHABET L"ΑΒΓΔΕΖΗΘΙΚΛΜΝΞΟΠΡΣΤΥΦΧΨΩαβγδεζηθικλμνξοπρσςτυφϕχψω"
#define RADIX 50
#endif

/**
 Persian alphabet (RTL)
 */
#if CUSTOM_ALPHABET == 20
#define ALPHABET L"ءئؤآابپتثجچحخدذرزژسشصضطظعغفقکگلمنوهی"
#define RADIX 36
#endif

/*
 * It is mandatory to determine these constants for the alphabet. You can either
 * pre-calculate the logarithm value (with at least 10 significant digits) and
 * set it as a constant, or let it be calculated dynamically like this:
 */
#include <math.h>
#define LOGRDX (log(RADIX) / log(2)) /*  log2( RADIX ) if std=C99 */
#if FF_X == 3
#define MAXLEN (2 * (int) (96.000001 / LOGRDX))
#endif
#define MINLEN ((int) (19.931568 / LOGRDX + 1))

#else
#define ALPHABET "0123456789"
#define string_t char *    /*  string pointer type      */
#define RADIX 10           /*  strlen (ALPHABET)        */
#define LOGRDX 3.321928095 /*  log2 (RADIX)             */
#define MINLEN 6           /*  ceil (6 / log10 (RADIX)) */
#define MAXLEN 56          /*  only if FF_X == 3        */
#endif

#if RADIX > 0x100
typedef unsigned short rbase_t; /*  digit type in base-radix */
#else
typedef uint8_t rbase_t;
#endif

#if FF_X != 3 /*  FF1 method:              */

static size_t bb, dd; /*  b and d constants in FF1 */

/** convert a string `s` in base-RADIX to a big-endian number, denoted by num */
static void numRadix(const rbase_t *s, size_t len, uint8_t *num, size_t bytes)
{
    memset(num, 0, bytes);
    while (len--) {
        size_t i, y = *s++;
        for (i = bytes; i; y >>= 8) {
            y += num[--i] * RADIX; /*  num = num * RADIX + y    */
            num[i] = (uint8_t) y;
        }
    }
}

/** convert a big-endian number to its base-RADIX representation string: `s`  */
static void strRadix(const uint8_t *num, size_t bytes, rbase_t *s, size_t len)
{
    memset(s, 0, sizeof(rbase_t) * len);
    while (bytes--) {
        size_t i, x = *num++;
        for (i = len; i; x /= RADIX) {
            x += s[--i] << 8; /*  numstr = numstr << 8 + x */
            s[i] = x % RADIX;
        }
    }
}

/** add two numbers in base-RADIX represented by q and p, so that: p = p + q  */
static void rbase_add(const rbase_t *q, size_t N, rbase_t *p)
{
    size_t c, a;
    for (c = 0; N--; c = a >= RADIX) /*  big-endian addition      */
    {
        a = p[N] + q[N] + c;
        p[N] = a % RADIX;
    }
}

/** subtract two numbers in base-RADIX represented by q and p, so that p -= q */
static void rbase_sub(const rbase_t *q, size_t N, rbase_t *p)
{
    size_t c, s;
    for (c = 0; N--; c = s < RADIX) /*  big-endian subtraction   */
    {
        s = RADIX + p[N] - q[N] - c;
        p[N] = s % RADIX;
    }
}

/** apply the FF1 round at step `i` to the input string X with length `len`   */
static void FF1round(const uint8_t i,
                     const block_t P,
                     const size_t u,
                     const size_t len,
                     rbase_t *Xc)
{
    size_t k = bb % BLOCKSIZE, s = i & 1 ? len : len - u;
    block_t R = {0};
    uint8_t *num = (void *) (Xc + u); /* use pre-allocated memory  */

    numRadix(Xc - s, len - u, num, bb); /* get NUM_radix(B)          */
    memcpy(R + BLOCKSIZE - k, num, k);  /* feed NUMradix(B) into PRF */
    R[LAST - k] = i;
    xMac(P, BLOCKSIZE, R, &rijndaelEncrypt, R);
    xMac(num + k, bb - k, R, &rijndaelEncrypt, R);

    memcpy(num, R, sizeof R); /* R = PRF(P || Q)           */
    k = (dd - 01) / sizeof R; /* total additional blocks   */
    for (num += k * sizeof R; k; --k) {
        memcpy(num, R, sizeof R);
        xor2BVal(num, k, LAST);    /* num = R || R ^ [j] ||...  */
        rijndaelEncrypt(num, num); /* S = R || Enc(R ^ [j])...  */
        num -= sizeof R;
    }
    strRadix(num, dd, Xc, u); /* take first d bytes of S   */
}

/** encrypt/decrypt a base-RADIX string X with length len using FF1 algorithm */
static void FF1_Cipher(const uint8_t *key,
                       const char mode,
                       const size_t len,
                       const uint8_t *tweak,
                       const size_t tweakLen,
                       rbase_t *X)
{
    size_t t, u = (len + !mode) / 2;
    rbase_t *Xc = X + len;

    block_t P = {1, 2, 1, RADIX >> 16, RADIX >> 8 & 0xFF, RADIX & 0xFF, 10};
    uint8_t n = 10 * mode, i = 0, r = tweakLen % BLOCKSIZE;

    if (r > (uint8_t) ~bb % BLOCKSIZE)
        r = 0;

    P[7] = len / 2 & 0xFF;
    xor2BVal(P, len, 11);
    xor2BVal(P, tweakLen, LAST); /* P = [1,2,1][radix][10]... */
    t = tweakLen - r;

    AES_SetKey(key);
    rijndaelEncrypt(P, P);
    xMac(tweak, t, P, &rijndaelEncrypt, P); /* P = PRF(P || tweak)       */

    while (i < r)
        P[i++] ^= tweak[t++];

    for (i = 0x0; i < n; u = len - u) /* Feistel procedure         */
    {
        FF1round(i++, P, u, len, Xc); /* encryption rounds         */
        rbase_add(Xc, u, i & 1 ? X : Xc - u);
    }
    for (i ^= 10; i > 0; u = len - u) /* A → X, C → Xc, B → Xc - u */
    {
        FF1round(--i, P, u, len, Xc); /* decryption rounds         */
        rbase_sub(Xc, u, i & 1 ? Xc - u : X);
    }
}
#else                  /*         FF3/FF3-1         */

/** converts a string in base-RADIX to a little-endian number, denoted by num */
static void numRadix(const rbase_t *s, uint8_t len, uint8_t *num, uint8_t bytes)
{
    memset(num, 0, bytes);
    while (len--) {
        size_t i, d = s[len];
        for (i = 0; i < bytes; d >>= 8) {
            d += num[i] * RADIX; /*  num = num * RADIX + d    */
            num[i++] = (uint8_t) d;
        }
    }
}

/** convert a little-endian number to its base-RADIX representation string: s */
static void strRadix(const uint8_t *num, uint8_t bytes, rbase_t *s, uint8_t len)
{
    memset(s, 0, sizeof(rbase_t) * len);
    while (bytes--) {
        size_t i, b = num[bytes];
        for (i = 0; i < len; b /= RADIX) {
            b += s[i] << 8; /*  numstr = numstr << 8 + b */
            s[i++] = b % RADIX;
        }
    }
}

/** add two numbers in base-RADIX represented by q and p, so that: p = p + q  */
static void rbase_add(const rbase_t *q, const uint8_t N, rbase_t *p)
{
    size_t i, c, a;
    for (i = c = 0; i < N; c = a >= RADIX) /* little-endian addition    */
    {
        a = p[i] + q[i] + c;
        p[i++] = a % RADIX;
    }
}

/** subtract two numbers in base-RADIX represented by q and p, so that p -= q */
static void rbase_sub(const rbase_t *q, const uint8_t N, rbase_t *p)
{
    size_t i, c, s;
    for (i = c = 0; i < N; c = s < RADIX) /* little-endian subtraction */
    {
        s = RADIX + p[i] - q[i] - c;
        p[i++] = s % RADIX;
    }
}

/** apply the FF3-1 round at step `i` to the input string X with length `len` */
static void FF3round(const uint8_t i,
                     const uint8_t *T,
                     const uint8_t u,
                     const uint8_t len,
                     rbase_t *X)
{
    uint8_t const w = (i & 1) * 4, s = i & 1 ? len : len - u;
    block_t P;
    COPY32BIT(T[w], P[12]); /*  W = (i is odd) ? TR : TL */

    numRadix(X - s, len - u, P, 12); /*  get REV. NUM_radix( B )  */
    P[12] ^= i;
    rijndaelEncrypt(P, P);
    strRadix(P, sizeof P, X, u); /*  C = *X = REV. STR_m( c ) */
}

/** encrypt/decrypt a base-RADIX string X with size len using FF3-1 algorithm */
static void FF3_Cipher(const uint8_t *key,
                       const char mode,
                       const uint8_t len,
                       const uint8_t *tweak,
                       rbase_t *X)
{
    rbase_t *Xc = X + len;
    uint8_t T[8], n = 8 * mode, u = (len + mode) / 2, *k = (void *) Xc, i;

    memcpy(k, tweak, FF3_TWEAK_LEN);
#if FF3_TWEAK_LEN == 7 /*  old version of FF3 had a */
    k[7] = (uint8_t) (k[3] << 4); /* ..64-bit tweak. but FF3-1 */
    k[3] &= 0xF0;                 /* ..tweaks must be 56-bit   */
#endif

    for (i = sizeof T; i--;)
        T[i] = k[7 - i];
    for (i = KEYSIZE; i--;)
        k[i] = *key++; /*  key/tweak are reversed   */

    AES_SetKey(k);
    SABOTAGE(k, KEYSIZE);

    for (i = 0x0; i < n; u = len - u) /*  Feistel procedure        */
    {
        FF3round(i++, T, u, len, Xc); /*  encryption rounds        */
        rbase_add(Xc, u, i & 1 ? X : Xc - u);
    }
    for (i ^= 8; i != 0; u = len - u) /* A → X, C → Xc, B → Xc - u */
    {
        FF3round(--i, T, u, len, Xc); /*  decryption rounds        */
        rbase_sub(Xc, u, i & 1 ? Xc - u : X);
    }
}
#endif /* FF_X */

/*----------------------------------------------------------------------------*\
                            FPE-AES: main functions
\*----------------------------------------------------------------------------*/
#include <stdlib.h>

/** allocate the required memory and validate the input string in FPE mode... */
static char FPEinit(const string_t str, const size_t len, rbase_t **indices)
{
    const string_t alpha = ALPHABET;
    size_t i = (len + 1) / 2;
    size_t j = (len + i) * sizeof(rbase_t);

#if FF_X != 3
    bb = (size_t) (LOGRDX * i + 8 - 1e-10) / 8; /*  extra memory is required */
    dd = (bb + 7) & ~3UL;                       /*  ..whose size is at least */
    j += (dd + 12) & ~15UL;                     /*  ..ceil( d/16 ) blocks    */
#else
    i *= len > MAXLEN ? 0 : sizeof(rbase_t);
    j += i >= KEYSIZE ? 0 : KEYSIZE - i;
#endif

    if (len < MINLEN || i == 0)
        return 'L'; /*  invalid string-length    */

    *indices = malloc(j);
    if (*indices == NULL)
        return 'M'; /*  memory allocation failed */

    for (i = 0; i < len; ++i) {
        for (j = 0; alpha[j] != str[i];) {
            if (++j == RADIX) {
                free(*indices); /*  invalid character found  */
                return 'C';
            }
        }
        (*indices)[i] = (rbase_t) j;
    }
    return 0;
}

/** make the output string after completing FPE encrypt/decryption procedures */
static void FPEfinalize(const rbase_t *index, const size_t len, void *output)
{
    size_t i;
    string_t str = output;
    const string_t abc = ALPHABET;
    BURN(RoundKey);

    str[len] = 0; /*  null-terminated strings? */
    for (i = len; i--;)
        str[i] = abc[index[i]];
}

/**
 * @brief   encrypt the input string using FPE-AES block-cipher method
 * @param   key       encryption key with a fixed size specified by KEYSIZE
 * @param   tweak     tweak byte array; similar to nonce in other schemes
 * @param   tweakLen  size of tweak. must be exactly 7 in FF3-1
 * @param   pntxt     input plaintext string, consisted of ALPHABET characters
 * @param   ptextLen  size of plaintext string, or number of characters
 * @param   crtxt     resulting ciphertext string
 * @return            whether all conditions of the algorithm were satisfied
 */
char AES_FPE_encrypt(const uint8_t *key,
                     const uint8_t *tweak,
#if FF_X != 3
                     const size_t tweakLen,
#endif
                     const void *pntxt,
                     const size_t ptextLen,
                     void *crtxt)
{
    rbase_t *index = NULL;
    if (FPEinit(pntxt, ptextLen, &index) != 0)
        return ENCRYPTION_FAILURE;

#if FF_X == 3
    FF3_Cipher(key, 1, ptextLen, tweak, index);
#else
    FF1_Cipher(key, 1, ptextLen, tweak, tweakLen, index);
#endif
    FPEfinalize(index, ptextLen, crtxt);
    free(index);
    return ENDED_IN_SUCCESS;
}

/**
 * @brief   decrypt a ciphertext string using FPE-AES block-cipher method
 * @param   key       decryption key with a fixed size specified by KEYSIZE
 * @param   tweak     tweak byte array; similar to nonce in other schemes
 * @param   tweakLen  size of tweak. must be exactly 7 in FF3-1
 * @param   crtxt     input ciphertext string, consisted of ALPHABET characters
 * @param   crtxtLen  size of ciphertext string, or number of characters
 * @param   pntxt     resulting plaintext string
 * @return            whether all conditions of the algorithm were satisfied
 */
char AES_FPE_decrypt(const uint8_t *key,
                     const uint8_t *tweak,
#if FF_X != 3
                     const size_t tweakLen,
#endif
                     const void *crtxt,
                     const size_t crtxtLen,
                     void *pntxt)
{
    rbase_t *index = NULL;
    if (FPEinit(crtxt, crtxtLen, &index) != 0)
        return DECRYPTION_FAILURE;

#if FF_X == 3
    FF3_Cipher(key, 0, crtxtLen, tweak, index);
#else
    FF1_Cipher(key, 0, crtxtLen, tweak, tweakLen, index);
#endif
    FPEfinalize(index, crtxtLen, pntxt);
    free(index);
    return ENDED_IN_SUCCESS;
}
#endif /* FPE */

#define HEXSTR_LENGTH 114 /* plaintext hex characters */
#include <stdio.h>

static const char *
    secretKey =
       "0001020304050607 08090A0B0C0D0E0F 1011121314151617 18191A1B1C1D1E1F",
   *secondKey =
       "0011223344556677 8899AABBCCDDEEFF 0001020304050607 08090A0B0C0D0E0F",
   *cipherKey =
       "279fb74a7572135e 8f9b8ef6d1eee003 69c4e0d86a7b0430 d8cdb78070b4c55a",
   *plainText =
       "c9f775baafa36c25 cd610d3c75a482ea dda97ca4864cdfe0 6eaf70a0ec0d7191"
       "d55027cf8f900214 e634412583ff0b47 8ea2b7ca516745bf ea",
   *iVec = "8ea2b7ca516745bf eafc49904b496089",
#if AES___ == 256
   *k_wrapped =
       "28C9F404C4B810F4 CBCCB35CFB87F826 3F5786E2D80ED326 CBC7F0E71A99F43B"
       "FB988B9B7A02DD21", /*  <---- p. 34 of RFC-3394 */
       *xtscipher =
           "40bfcc14845b1bb4 15dd13abf1e6f89d 3bfd794cf6655ffd 14c0d7e4177eeaf4"
           "5dd95f05663fcfb4 47671154a91b9d00 d1bd7a35c14c7410 9a";
#elif AES___ == 192 /*  ↓↓↓↓  if PKCS#7 enabled */
   *ecbcipher =
       "af1893f0fbb09a43 7f6b0fd4f4977890 7bb85cccf1e9d2e3 ebe5bae935107868"
       "c6d72cb2ca375c12 ce6b6b1141141fd0 d268d14db351d680 5aabb99427341da9",
   *k_wrapped =
       "031D33264E15D332 68F24EC260743EDC E1C6C7DDEE725A93 6BA814915C6762D2";
#else               /*  ↓↓↓↓  zero-padded input */
   *ecbcipher =
       "5d00c273f8b2607d a834632dcbb521f4 697dd4ab20bb0645 32a6545e24e33ae9"
       "f545176111f93773 dbecd262841cf83b 10d145e71b772cf7 a12889cda84be795",
#if CTS
   *cbccipher =
       "65c48fdf9fbd6261 28f2d8bac3f71251 75e7f4821fda0263 70011632779d7403"
       "c119ef461ac4e1bc 8a7e36bf92b3b3d1 7e9e2d298e154bc4 2d",
#else /*  ↓↓↓↓  zero-padded input */
   *cbccipher =
       "65c48fdf9fbd6261 28f2d8bac3f71251 75e7f4821fda0263 70011632779d7403"
       "7e9e2d298e154bc4 2dc7a9bc419b915d c119ef461ac4e1bc 8a7e36bf92b3b3d1",
#endif
   *xtscipher =
       "10f9301a157bfceb 3eb9e7bd38500b7e 959e21ba3cc1179a d7f7d7d99460e695"
       "5e8bcb177571c719 6de58ff28c381913 e7c82d0adfd90c45 ca",
   *cfbcipher =
       "edab3105e673bc9e b9102539a9f457bc 245c14e1bff81b5b 4a4a147c988cb0a6"
       "3f9c56525efbe64a 876ad1d761d3fc93 59fb4f5b2354acd4 90",
   *ofbcipher =
       "edab3105e673bc9e b9102539a9f457bc d28c8e4c92995f5c d9426926be1e775d"
       "e22b8ce4d0278b18 181b8bec93b9726f 959aa5d701d46102 f0",
#if CTR_IV_LENGTH == 16
   *ctrcipher =
       "edab3105e673bc9e b9102539a9f457bc f2e2606dfa3f93c5 c51b910a89cddb67"
       "191a118531ea0427 97626c9bfd370426 fdf3f59158bf7d4d 43",
#else
   *ctrcipher =
       "6c6bae886c235d8c 7997d45c1bf0bca2 48b4bca9eb396d1b f6945e5b7a4fc10f"
       "488cfe76fd5eaeff 2b8fb469f78fa61e 285e4cf9b9aee3d0 a8",
#endif
   *ccmcipher =
       "d2575123438338d7 0b2955537fdfcf41 729870884e85af15 f0a74975a72b337d"
       "04d426de87594b9a be3e6dcf07f21c99 db3999f81299d302 ad1e5ba683e9039a"
       "5483685f1bd2c3fa 3b", /*  <---- with 16 bytes tag */
       *gcmcipher =
           "5ceab5b7c2d6dede 555a23c7e3e63274 4075a51df482730b "
           "a31485ec987ddcc8"
           "73acdcfc6759a47b a424d838e7c0cb71 b9a4d8f4572e2141 "
           "18c8ab284ca845c1"
           "4394618703cddf3a fb", /*  <---- with 16 bytes tag */
           *sivcipher =
               "ff2537a371fba0bb ed11acf2a3631300 97964f088881bdbd "
               "f163e261afd158e6"
               "09272e759213c76a edc83a451d094c9e 06e2600e50a27cbb "
               "c0d9fad10eb6d369"
               "4614362e5cd68b90 a9", /*  16 bytes i.v. PREPENDED */
               *ocbcipher =
                   "fc254896eb785b05 dd87f240722dd935 61f5a0ef6aff2eb6 "
                   "5953da0b26257ed0"
                   "d69cb496e9a0cb1b f646151aa07e629a 28d99f0ffd7ea753 "
                   "5c39f440df33c988"
                   "c55cbcc8ac086ffa 23", /*  ↑↑↓↓  with 16 bytes tag */
                   *gsvcipher =
                       "2f1488496ada3f70 9760420ac72e5acf a977f6add4c55ac6 "
                       "85f1b9dff8f381e0"
                       "2a64bbdd64cdd778 525462949bb0b141 db908c5cfa365750 "
                       "3666f879ac879fcb"
                       "f25c15d496a1e6f7 f8",
#if EAXP      /*  ↓↓↓↓  with 4 bytes tag  */
   *eaxcipher =
       "f516e9c20069292c c51ba8b6403ddedf 5a34798f62187f58 d723fa33573fd80b"
       "f08ffbb09dadbd0b 6fa4812ca4bb5e6d db9a384943b36690 e81738a7a1",
#else         /*  ↓↓↓↓  with 16 bytes tag */
   *eaxcipher =
       "4e2fa1bef9ffc23f 6965ee7135981c91 af9bfe97a6b13c01 b8b99e114dda2391"
       "50661c618335a005 47cca55a8f22fbd5 ed5ab4b4a17d0aa3 29febd14ef271bae"
       "986810a504f01ec6 02",
#endif        /*  ↓↓ a large Prime Number */
   *fpe_plain = "122333444455555666666777777788888888999999999012345682747",
#if FF_X == 3 /*  <-- MAXLEN=56 if RDX=10 */
   *fpecipher = "0053317760589559020399280014720716878020198371161819152",
#else
   *fpecipher = "000260964766881620856103152534002821752468680082944565411",
#endif
   *ptextcmac = "b887df1fd8c239c3 e8a64d9822e21128",
   *poly_1305 = "3175bed9bd01821a 62d4c7bef26722be",
   *k_wrapped = "1FA68B0A8112B447 AEF34BD8FB5A7B82 9D3E862371D2CFE5";
#endif

enum buffer_sizes {
    PBYTES = HEXSTR_LENGTH / 2,
    PADDED = PBYTES + 15 & ~15,
    TAGGED = PBYTES + 16
};

static void hex2bytes(const char *hex, uint8_t *bytes)
{
    unsigned shl = 0;
    for (--bytes; *hex; ++hex) {
        if (*hex < '0' || 'f' < *hex)
            continue;
        if ((shl ^= 4) != 0)
            *++bytes = 0;
        *bytes |= (*hex % 16 + (*hex > '9') * 9) << shl;
    }
}

static void check(const char *method,
                  void *result,
                  const void *expected,
                  size_t size)
{
    int c = memcmp(expected, result, size);
    printf("AES-%d %s: %s\n", AES_KEY_SIZE * 8, method,
           c ? "FAILED :`(" : "PASSED!");
    memset(result, 0xcc, TAGGED);
}

int main()
{
    uint8_t iv[16], key[64], authKey[32], input[PADDED], test[TAGGED],
        output[TAGGED], *a = authKey + 1, sa = sizeof authKey - 1, sp = PBYTES;
    hex2bytes(cipherKey, key);
    hex2bytes(secondKey, key + 32);
    hex2bytes(secretKey, authKey);
    hex2bytes(iVec, iv);
    hex2bytes(plainText, input);
#if M_RIJNDAEL
    hex2bytes(iVec, input + 48);
    hex2bytes(secondKey, test);
    a = AES_KEY_SIZE == 16 ? key : input + (AES___ - 192) / 2;

    AES_Cipher(key + 32, 'E', authKey, output);
    AES_Cipher(authKey, 'E', key + 32, output + 16);
    check("Encryption test", output, a, 32);
    AES_Cipher(key + 32, 'D', a, output + 16);
    AES_Cipher(authKey, 'D', a + 16, output);
    check("Decryption test", output, test, 32);
    return 0;
#endif
    printf("Test results\n");

#if ECB && AES_KEY_SIZE + 8 * !AES_PADDING == 24
    hex2bytes(ecbcipher, test);
    AES_ECB_encrypt(key, input, sp, output);
    check("ECB encryption", output, test, sizeof input);
    AES_ECB_decrypt(key, test, sizeof input, output);
    check("ECB decryption", output, input, sp);
#endif
#if CBC && AES_KEY_SIZE == 16
    hex2bytes(cbccipher, test);
    AES_CBC_encrypt(key, iv, input, sp, output);
    check("CBC encryption", output, test, CTS ? sp : sizeof input);
    AES_CBC_decrypt(key, iv, test, CTS ? sp : sizeof input, output);
    check("CBC decryption", output, input, sp);
#endif
#if CFB && AES_KEY_SIZE == 16
    hex2bytes(cfbcipher, test);
    AES_CFB_encrypt(key, iv, input, sp, output);
    check("CFB encryption", output, test, sp);
    AES_CFB_decrypt(key, iv, test, sp, output);
    check("CFB decryption", output, input, sp);
#endif
#if OFB && AES_KEY_SIZE == 16
    hex2bytes(ofbcipher, test);
    AES_OFB_encrypt(key, iv, input, sp, output);
    check("OFB encryption", output, test, sp);
    AES_OFB_decrypt(key, iv, test, sp, output);
    check("OFB decryption", output, input, sp);
#endif
#if CTR_NA && AES_KEY_SIZE == 16
    hex2bytes(ctrcipher, test);
    AES_CTR_encrypt(key, iv, input, sp, output);
    check("CTR encryption", output, test, sp);
    AES_CTR_decrypt(key, iv, test, sp, output);
    check("CTR decryption", output, input, sp);
#endif
#if XTS && AES_KEY_SIZE != 24
    hex2bytes(xtscipher, test);
    AES_XTS_encrypt(key, iv, input, sp, output);
    check("XTS encryption", output, test, sp);
    AES_XTS_decrypt(key, iv, test, sp, output);
    check("XTS decryption", output, input, sp);
#endif
#if CMAC && AES_KEY_SIZE == 16
    hex2bytes(ptextcmac, test);
    AES_CMAC(key, input, sp, output);
    check("plaintext CMAC", output, test, 16);
#endif
#if POLY1305 && AES_KEY_SIZE == 16
    hex2bytes(poly_1305, test);
    AES_Poly1305(key, iv, input, sp, output);
    check("Poly1305 auth.", output, test, 16);
#endif
#if GCM && AES_KEY_SIZE == 16
    hex2bytes(gcmcipher, test);
    AES_GCM_encrypt(key, iv, input, sp, a, sa, output, output + sp);
    check("GCM encryption", output, test, sp + 16);
    AES_GCM_decrypt(key, iv, test, sp, a, sa, 16, output);
    check("GCM decryption", output, input, sp);
#endif
#if CCM && AES_KEY_SIZE == 16
    hex2bytes(ccmcipher, test);
    AES_CCM_encrypt(key, iv, input, sp, a, sa, output, output + sp);
    check("CCM encryption", output, test, sp + CCM_TAG_LEN);
    *output ^= AES_CCM_decrypt(key, iv, test, sp, a, sa, CCM_TAG_LEN, output);
    check("CCM decryption", output, input, sp);
#endif
#if OCB && AES_KEY_SIZE == 16
    hex2bytes(ocbcipher, test);
    AES_OCB_encrypt(key, iv, input, sp, a, sa, output, output + sp);
    check("OCB encryption", output, test, sp + OCB_TAG_LEN);
    *output ^= AES_OCB_decrypt(key, iv, test, sp, a, sa, OCB_TAG_LEN, output);
    check("OCB decryption", output, input, sp);
#endif
#if SIV && AES_KEY_SIZE == 16
    hex2bytes(sivcipher, test);
    AES_SIV_encrypt(key, input, sp, a, sa, output, output + 16);
    check("SIV encryption", output, test, sp + 16);
    *output ^= AES_SIV_decrypt(key, test, test + 16, sp, a, sa, output);
    check("SIV decryption", output, input, sp);
#endif
#if GCM_SIV && AES_KEY_SIZE == 16
    hex2bytes(gsvcipher, test);
    GCM_SIV_encrypt(key, iv, input, sp, a, sa, output, output + sp);
    check("GCMSIV encrypt", output, test, sp + 16);
    *output ^= GCM_SIV_decrypt(key, iv, test, sp, a, sa, 16, output);
    check("GCMSIV decrypt", output, input, sp);
#endif
#if EAX && AES_KEY_SIZE == 16
    hex2bytes(eaxcipher, test);
#if EAXP
    AES_EAX_encrypt(key, a, input, sp, sa, output);
    check("EAX encryption", output, test, sp + 4);
    AES_EAX_decrypt(key, a, test, sp, sa, output);
#else
    AES_EAX_encrypt(key, iv, input, sp, a, sa, output, output + sp);
    check("EAX encryption", output, test, sp + 16);
    AES_EAX_decrypt(key, iv, test, sp, a, sa, 16, output);
#endif
    check("EAX decryption", output, input, sp);
#endif
#if FPE && AES_KEY_SIZE + CUSTOM_ALPHABET == 16
    memcpy(test, fpecipher, FF_X == 3 ? (sp = 55) : sp);
#if FF_X == 3
    AES_FPE_encrypt(key, a, fpe_plain, sp, output);
    check("FF3 encryption", output, test, sp);
    AES_FPE_decrypt(key, a, test, sp, output);
#else
    AES_FPE_encrypt(key, a, sa, fpe_plain, sp, output);
    check("FF1 encryption", output, test, sp);
    AES_FPE_decrypt(key, a, sa, test, sp, output);
#endif
    check("FPE decryption", output, fpe_plain, sp);
#endif
#if KWA
    hex2bytes(k_wrapped, test);
    AES_KEY_wrap(authKey, key + 32, AES_KEY_SIZE, output);
    check("key wrapping  ", output, test, AES_KEY_SIZE + 8);
    AES_KEY_unwrap(authKey, test, AES_KEY_SIZE + 8, output);
    check("key unwrapping", output, key + 32, AES_KEY_SIZE);
#endif
    /** a template for "OFFICIAL TEST VECTORS":  */
#if OCB * EAX * SIV * GCM_SIV * POLY1305 * FPE * (16 / AES_KEY_SIZE)
    printf("+-> Let's do some extra tests\n");

    sp = sa = 24; /* taken from RFC 7253:  */
    hex2bytes("000102030405060708090A0B0C0D0E0F", key);
    hex2bytes("BBAA99887766554433221107", iv);
    hex2bytes("000102030405060708090A0B0C0D0E0F1011121314151617", a);
    hex2bytes("000102030405060708090A0B0C0D0E0F1011121314151617", input);
    hex2bytes(
        "1CA2207308C87C010756104D8840CE1952F09673A448A122\
               C92C62241051F57356D7F3C90BB0E07F",
        test);
    AES_OCB_encrypt(key, iv, input, sp, a, sa, output, output + sp);
    check("OCB encryption", output, test, sp + OCB_TAG_LEN);
    *output ^= AES_OCB_decrypt(key, iv, test, sp, a, sa, OCB_TAG_LEN, output);
    check("OCB decryption", output, input, sp);

    sp = 11;
    sa = 7; /* taken from RFC 8452:  */
    hex2bytes("ee8e1ed9ff2540ae8f2ba9f50bc2f27c", key);
    hex2bytes("752abad3e0afb5f434dc4310", iv);
    hex2bytes("6578616d706c65", a);
    hex2bytes("48656c6c6f20776f726c64", input);
    hex2bytes("5d349ead175ef6b1def6fd4fbcdeb7e4793f4a1d7e4faa70100af1", test);
    GCM_SIV_encrypt(key, iv, input, sp, a, sa, output, output + sp);
    check("GCMSIV encrypt", output, test, sp + 16);
    *output ^= GCM_SIV_decrypt(key, iv, test, sp, a, sa, 16, output);
    check("GCMSIV decrypt", output, input, sp);
    sp = 12;
    sa = 1; /* taken from RFC 8452:  */
    hex2bytes("01000000000000000000000000000000", key);
    hex2bytes("030000000000000000000000", iv);
    hex2bytes("01", a);
    hex2bytes("020000000000000000000000", input);
    hex2bytes(
        "296c7889fd99f41917f4462008299c51\
               02745aaa3a0c469fad9e075a",
        test);
    GCM_SIV_encrypt(key, iv, input, sp, a, sa, output, output + sp);
    check("GCMSIV encrypt", output, test, sp + 16);
    *output ^= GCM_SIV_decrypt(key, iv, test, sp, a, sa, 16, output);
    check("GCMSIV decrypt", output, input, sp);

    sp = 14;
    sa = 24; /* taken from RFC 5297:  */
    hex2bytes(
        "fffefdfc fbfaf9f8 f7f6f5f4 f3f2f1f0\
               f0f1f2f3 f4f5f6f7 f8f9fafb fcfdfeff",
        key);
    hex2bytes(
        "10111213 14151617 18191a1b 1c1d1e1f\
               20212223 24252627",
        a);
    hex2bytes("11223344 55667788 99aabbcc ddee", input);
    hex2bytes(
        "85632d07 c6e8f37f 950acd32 0a2ecc93\
               40c02b96 90c4dc04 daef7f6a fe5c",
        test);
    AES_SIV_encrypt(key, input, sp, a, sa, output, output + 16);
    check("SIV encryption", output, test, sp + 16);
    *output ^= AES_SIV_decrypt(key, test, test + 16, sp, a, sa, output);
    check("SIV decryption", output, input, sp);
    sp = 16;
    sa = 0; /* from miscreant on github: bit.ly/3ycgGB */
    hex2bytes(
        "fffefdfcfbfaf9f8f7f6f5f4f3f2f1f0f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff",
        key);
    hex2bytes("00112233445566778899aabbccddeeff", input);
    hex2bytes(
        "f304f912863e303d5b540e5057c7010c942ffaf45b0e5ca5fb9a56a5263bb065",
        test);
    AES_SIV_encrypt(key, input, sp, a, sa, output, output + 16);
    check("SIV encryption", output, test, sp + 16);
    *output ^= AES_SIV_decrypt(key, test, test + 16, sp, a, sa, output);
    check("SIV decryption", output, input, sp);
#if EAXP
    sp = 0;
    sa = 50; /* from Annex G of the IEEE Std 1703-2012  */
    hex2bytes("01020304050607080102030405060708", key);
    hex2bytes(
        "A20D060B607C86F7540116007BC175A8\
               03020100BE0D280B810984A60C060A60\
               7C86F7540116007B040248F3C2040330\
               0005",
        input);
    hex2bytes("515AE775", test);
    AES_EAX_encrypt(key, input, NULL, sp, sa, output);
    check("EAX encryption", output, test, sp + 4);
    sp += AES_EAX_decrypt(key, input, test, sp, sa, output);
    check("EAX decryption", output, input, sp);
    sp = 28;
    sa = 65; /* from Moise-Beroset-Phinney-Burns paper: */
    hex2bytes("10 20 30 40 50 60 70 80 90 a0 b0 c0 d0 e0 f0 00", authKey);
    hex2bytes(
        "a2 0e 06 0c 60 86 48 01 86 fc 2f 81 1c aa 4e 01\
               a8 06 02 04 39 a0 0e bb ac 0f a2 0d a0 0b a1 09\
               80 01 00 81 04 4b ce e2 c3 be 25 28 23 81 21 88\
               a6 0a 06 08 2b 06 01 04 01 82 85 63 00 4b ce e2\
               c3",
        test);
    hex2bytes(
        "17 51 30 30 30 30 30 30 30 30 30 30 30 30 30 30\
               30 30 30 30 30 30 00 00 03 30 00 01",
        input);
    hex2bytes(
        "9c f3 2c 7e c2 4c 25 0b e7 b0 74 9f ee e7 1a 22\
               0d 0e ee 97 6e c2 3d bf 0c aa 08 ea 00 54 3e 66",
        key);
    AES_EAX_encrypt(authKey, test, input, sp, sa, output);
    check("EAX encryption", output, key, sp + 4);
    AES_EAX_decrypt(authKey, test, key, sp, sa, output);
#else
    sp = 12;
    sa = 8; /* from Bellare-Rogaway-Wagner 2004 paper: */
    hex2bytes("BD8E6E11475E60B268784C38C62FEB22", key);
    hex2bytes("6EAC5C93072D8E8513F750935E46DA1B", iv);
    hex2bytes("D4482D1CA78DCE0F", a);
    hex2bytes("4DE3B35C3FC039245BD1FB7D", input);
    hex2bytes("835BB4F15D743E350E728414ABB8644FD6CCB86947C5E10590210A4F", test);
    AES_EAX_encrypt(key, iv, input, sp, a, sa, output, output + sp);
    check("EAX encryption", output, test, sp + 16);
    AES_EAX_decrypt(key, iv, test, sp, a, sa, 16, output);
#endif
    check("EAX decryption", output, input, sp);

#if FF_X == 3 && !CUSTOM_ALPHABET
    sp = 29; /* zero tweak works for both FF3 and FF3-1 */
    hex2bytes("EF 43 59 D8 D5 80 AA 4F 7F 03 6D 6F 04 FC 6A 94", key);
    hex2bytes("00 00 00 00 00 00 00 00", a);
    memcpy(input, "89012123456789000000789000000", sp);
    memcpy(output, "34695224821734535122613701434", sp);
    AES_FPE_encrypt(key, a, input, sp, test);
    check("FF3 encryption", test, output, sp);
    AES_FPE_decrypt(key, a, output, sp, test);
    check("FF3 decryption", test, input, sp);
#elif FF_X != 3 && CUSTOM_ALPHABET == 3
    sp = 19;
    sa = 11;
    hex2bytes("2B 7E 15 16 28 AE D2 A6 AB F7 15 88 09 CF 4F 3C", key);
    hex2bytes("37 37 37 37 70 71 72 73 37 37 37", a);
    memcpy(input, "0123456789abcdefghi", sp);
    memcpy(output, "a9tv40mll9kdu509eum", sp);
    AES_FPE_encrypt(key, a, sa, input, sp, test);
    check("FF1 encryption", test, output, sp);
    AES_FPE_decrypt(key, a, sa, output, sp, test);
    check("FF1 decryption", test, input, sp);
#endif
    sp = 32; /* ↓ D.J.B [2005] paper: cr.yp.to/mac.html */
    hex2bytes(
        "66 3c ea 19 0f fb 83 d8 95 93 f3 f4 76 b6 bc 24\
               d7 e6 79 10 7e a2 6a db 8c af 66 52 d0 65 61 36",
        input);
    hex2bytes(
        "6a cb 5f 61 a7 17 6d d3 20 c5 c1 eb 2e dc dc 74\
               48 44 3d 0b b0 d2 11 09 c8 9a 10 0b 5c e2 c2 08",
        key);
    hex2bytes("ae 21 2a 55 39 97 29 59 5d ea 45 8b c6 21 ff 0e", iv);
    hex2bytes("0e e1 c1 6b b7 3f 0f 4f d1 98 81 75 3c 01 cd be", test);
    AES_Poly1305(key, iv, input, sp, output);
    check("Poly1305 auth.", output, test, 16);
    sp = 63;
    hex2bytes(
        "ab 08 12 72 4a 7f 1e 34 27 42 cb ed 37 4d 94 d1\
               36 c6 b8 79 5d 45 b3 81 98 30 f2 c0 44 91 fa f0\
               99 0c 62 e4 8b 80 18 b2 c3 e4 a0 fa 31 34 cb 67\
               fa 83 e1 58 c9 94 d9 61 c4 cb 21 09 5c 1b f9",
        input);
    hex2bytes(
        "e1 a5 66 8a 4d 5b 66 a5 f6 8c c5 42 4e d5 98 2d\
               12 97 6a 08 c4 42 6d 0c e8 a8 24 07 c4 f4 82 07",
        key);
    hex2bytes("9a e8 31 e7 43 97 8d 3a 23 52 7c 71 28 14 9e 3a", iv);
    hex2bytes("51 54 ad 0d 2c b2 6e 01 27 4f c5 11 48 49 1f 1b", test);
    AES_Poly1305(key, iv, input, sp, output);
    check("Poly1305 auth.", output, test, 16);
#endif
    return 0;
}
