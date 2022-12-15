/*
 * Copyright © 2022 - polfosol
 * µAES - A minimalist ANSI-C compatible code for the AES encryption and block
 * cipher modes.
 */

/** If your desired alphabet contains non-ASCII characters, the CUSTOM_ALPHABET
 * macro 'must be' set to a double-digit number, e.g 21. Please note that ANSI-C
 * standard does not support such characters and the code loses its compliance
 * in this case. In what follows, you will find some sample alphabets along with
 * their corresponding macro definitions. It is straightforward to set another
 * custom alphabet according to these samples.
 */
#define NON_ASCII_CHARACTER_SET (CUSTOM_ALPHABET >= 10)

#if NON_ASCII_CHARACTER_SET
#include <locale.h>
#include <wchar.h>
#define string_t wchar_t *
#else
#define string_t char * /*  string pointer type      */
#endif

#if CUSTOM_ALPHABET == 0
#define ALPHABET "0123456789"
#define RADIX 10 /*  strlen (ALPHABET)        */
#endif

/**----------------------------------------------------------------------------
 binary strings
 -----------------------------------------------------------------------------*/
#if CUSTOM_ALPHABET == 1
#define ALPHABET "01"
#define RADIX 2
#endif

/**----------------------------------------------------------------------------
 lowercase english letters
 -----------------------------------------------------------------------------*/
#if CUSTOM_ALPHABET == 2
#define ALPHABET "abcdefghijklmnopqrstuvwxyz"
#define RADIX 26
#endif

/**----------------------------------------------------------------------------
 lowercase alphanumeric strings
 -----------------------------------------------------------------------------*/
#if CUSTOM_ALPHABET == 3
#define ALPHABET "0123456789abcdefghijklmnopqrstuvwxyz"
#define RADIX 36
#endif

/**----------------------------------------------------------------------------
 the English alphabet
 -----------------------------------------------------------------------------*/
#if CUSTOM_ALPHABET == 4
#define ALPHABET "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
#define RADIX 52
#endif

/**----------------------------------------------------------------------------
 base-64 encoded strings (RFC-4648), with no padding character
 -----------------------------------------------------------------------------*/
#if CUSTOM_ALPHABET == 5
#define ALPHABET \
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
#define RADIX 64
#endif

/**----------------------------------------------------------------------------
 base-85 encoded strings (RFC-1924)
 -----------------------------------------------------------------------------*/
#if CUSTOM_ALPHABET == 6
#define ALPHABET                                                               \
    "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz!#$%&()*+-" \
    ";<=>?@^_`{|}~"
#define RADIX 85
#endif

/**----------------------------------------------------------------------------
 a character set with length 26, used by some test vectors
 -----------------------------------------------------------------------------*/
#if CUSTOM_ALPHABET == 7
#define ALPHABET "0123456789abcdefghijklmnop"
#define RADIX 26
#endif

/**----------------------------------------------------------------------------
 base-64 character set with different ordering, used by some test vectors
 -----------------------------------------------------------------------------*/
#if CUSTOM_ALPHABET == 8
#define ALPHABET \
    "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz+/"
#define RADIX 64
#endif

/**----------------------------------------------------------------------------
 Greek alphabet
 -----------------------------------------------------------------------------*/
#if CUSTOM_ALPHABET == 10
#define ALPHABET L"ΑΒΓΔΕΖΗΘΙΚΛΜΝΞΟΠΡΣΤΥΦΧΨΩαβγδεζηθικλμνξοπρσςτυφχψω"
#define RADIX 49
#endif

/**----------------------------------------------------------------------------
 Persian alphabet
 -----------------------------------------------------------------------------*/
#if CUSTOM_ALPHABET == 11
#define ALPHABET L"ءئاآبپتثجچحخدذرزژسشصضطظعغفقکگلمنوهی"
#define RADIX 35
#endif

/*
 It is mandatory to determine these constants for each alphabet. You can either
 pre-calculate the logarithm value (with at least 10 significant digits) and
 set it as a constant, or let it be calculated dynamically like this:
*/
#include <math.h>
#define LOGRDX (log(RADIX) / log(2)) /*  log2( RADIX ) if std=C99 */
#if FF_X == 3
#define MAXLEN (2 * (int) (96.000001 / LOGRDX))
#endif
#define MINLEN ((int) (19.931568 / LOGRDX + 1))

/**----------------------------------------------------------------------------
You can use different AES algorithms by changing this macro. Default is AES-128
 -----------------------------------------------------------------------------*/
#define AES___ 128 /* or 256 (or 192; not standardized in some modes) */

/**----------------------------------------------------------------------------
AES block-cipher modes of operation. The following modes can be enabled/disabled
 by setting their corresponding macros to TRUE (1) or FALSE (0).
 -----------------------------------------------------------------------------*/
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

#define WTF !(POLY1305 || CMAC || BLOCKCIPHERS)
#define M_RIJNDAEL WTF /* none of above; just rijndael API. dude.., why?  */

/**----------------------------------------------------------------------------
Refer to the BOTTOM OF THIS DOCUMENT for some explanations about these macros:
 -----------------------------------------------------------------------------*/

#if ECB || CBC || XEX || KWA || M_RIJNDAEL
#define DECRYPTION 1 /* only these modes need the decrypt part of AES   */
#endif

#if ECB || (CBC && !CTS) || (XEX && !XTS)
#define AES_PADDING 0 /* standard values:  (1) PKCS#7  (2) IEC7816-4     */
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
#define EAX_NONCE_LEN 16 /* no specified limit; can be arbitrarily large    */
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
#if INT_MAX > 100000L
typedef int int32_t;
#else
typedef long int32_t;
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**----------------------------------------------------------------------------
Encryption/decryption of a single block with Rijndael
 -----------------------------------------------------------------------------*/
#if M_RIJNDAEL
void AES_Cipher(const uint8_t *key, /* encryption/decryption key    */
                const char mode,    /* encrypt: 'E', decrypt: 'D'   */
                const uint8_t *x,   /* input block byte array       */
                uint8_t *y);        /* output block byte array      */
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
char AES_CBC_encrypt(const uint8_t *key,    /* encryption key               */
                     const uint8_t *iVec,   /* initialization vector        */
                     const uint8_t *pntxt,  /* plaintext buffer             */
                     const size_t ptextLen, /* length of input plain text   */
                     uint8_t *crtxt);       /* cipher-text result           */

char AES_CBC_decrypt(const uint8_t *key,    /* decryption key               */
                     const uint8_t *iVec,   /* initialization vector        */
                     const uint8_t *crtxt,  /* cipher-text buffer           */
                     const size_t crtxtLen, /* length of input cipher text  */
                     uint8_t *pntxt);       /* plaintext result             */
#endif                                      /* CBC */

/**----------------------------------------------------------------------------
Main functions for CFB-AES block ciphering
 -----------------------------------------------------------------------------*/
#if CFB
void AES_CFB_encrypt(const uint8_t *key,    /* encryption key               */
                     const uint8_t *iVec,   /* initialization vector        */
                     const uint8_t *pntxt,  /* plaintext buffer             */
                     const size_t ptextLen, /* length of input plain text   */
                     uint8_t *crtxt);       /* cipher-text result           */

void AES_CFB_decrypt(const uint8_t *key,    /* decryption key               */
                     const uint8_t *iVec,   /* initialization vector        */
                     const uint8_t *crtxt,  /* cipher-text buffer           */
                     const size_t crtxtLen, /* length of input cipher text  */
                     uint8_t *pntxt);       /* plaintext result             */
#endif                                      /* CFB */

/**----------------------------------------------------------------------------
Main functions for OFB-AES block ciphering
 -----------------------------------------------------------------------------*/
#if OFB
void AES_OFB_encrypt(const uint8_t *key,    /* encryption key               */
                     const uint8_t *iVec,   /* initialization vector        */
                     const uint8_t *pntxt,  /* plaintext buffer             */
                     const size_t ptextLen, /* length of input plain text   */
                     uint8_t *crtxt);       /* cipher-text result           */

void AES_OFB_decrypt(const uint8_t *key,    /* decryption key               */
                     const uint8_t *iVec,   /* initialization vector        */
                     const uint8_t *crtxt,  /* cipher-text buffer           */
                     const size_t crtxtLen, /* length of input cipher text  */
                     uint8_t *pntxt);       /* plaintext result             */
#endif                                      /* OFB */

/**----------------------------------------------------------------------------
Main functions for XTS-AES block ciphering
 -----------------------------------------------------------------------------*/
#if XTS
char AES_XTS_encrypt(const uint8_t *keys,   /* encryption key pair          */
                     const uint8_t *unitId, /* tweak value (sector ID)      */
                     const uint8_t *pntxt,  /* plaintext buffer             */
                     const size_t ptextLen, /* length of input plain text   */
                     uint8_t *crtxt);       /* cipher-text result           */

char AES_XTS_decrypt(const uint8_t *keys,   /* decryption key pair          */
                     const uint8_t *unitId, /* tweak value (sector ID)      */
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
                     uint8_t *iv,           /* synthesized initial-vector   */
                     uint8_t *crtxt);       /* cipher-text result           */

char AES_SIV_decrypt(const uint8_t *keys,   /* decryption key pair          */
                     const uint8_t *iv,     /* provided initial-vector      */
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
                     uint8_t *auTag);       /* message authentication tag   */

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
                     uint8_t *auTag);       /* message authentication tag   */

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
                     uint8_t *auTag);       /* message authentication tag   */

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
                     uint8_t *auTag);       /* message authentication tag   */
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
                     uint8_t *auTag);       /* 16-bytes mandatory tag       */

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
#if FF_X != 3
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
void AES_Poly1305(const uint8_t *keys,   /* encryption/mixing key pair   */
                  const uint8_t *nonce,  /* the 128-bit nonce            */
                  const void *data,      /* input data buffer            */
                  const size_t dataSize, /* size of data in bytes        */
                  uint8_t *mac);         /* calculated poly1305-AES mac  */
#endif

/**----------------------------------------------------------------------------
Main function for AES Cipher-based Message Authentication Code
 -----------------------------------------------------------------------------*/
#if CMAC
void AES_CMAC(const uint8_t *key,    /* encryption/cipher key        */
              const void *data,      /* input data buffer            */
              const size_t dataSize, /* size of data in bytes        */
              uint8_t *mac);         /* calculated CMAC hash         */
#endif

#ifdef __cplusplus
}
#endif

/**----------------------------------------------------------------------------
The error codes and key length should be defined here for external references:
 -----------------------------------------------------------------------------*/
#define ENCRYPTION_FAILURE 0x1E
#define DECRYPTION_FAILURE 0x1D
#define AUTHENTICATION_FAILURE 0x1A
#define ENDED_IN_SUCCESS 0x00

#if (AES___ == 256) || (AES___ == 192)
#define AES_KEY_LENGTH (AES___ / 8)
#else
#define AES_KEY_LENGTH 16
#endif

/**¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯**\
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
    enable the SMALL_CIPHER macro to save a few bytes in the compiled code. Note
    that for key-wrapping, this limit is 42 blocks (336 bytes) of secret key.
    These assumptions are likely to be valid for some embedded systems and small
    applications. Furthermore, enabling that other macro, REDUCE_CODE_SIZE had a
    considerable effect on the size of the compiled code in my own tests.
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

#define KEYSIZE AES_KEY_LENGTH
#define BLOCKSIZE (128 / 8) /* Block length in AES is 'always' 128-bits.  */
#define Nb (BLOCKSIZE / 4)  /* The number of columns comprising a AES state. */
#define Nk (KEYSIZE / 4)    /* The number of 32 bit words in a key.          */
#define LAST                                                            \
    (BLOCKSIZE - 1)     /* The index at the end of block, or last index \
                         */
#define ROUNDS (Nk + 6) /* The number of rounds in AES Cipher.           */

#define IMPLEMENT(x) (x) > 0

#define INCREASE_SECURITY 0 /* refer to the bottom of the header file for */
#define SMALL_CIPHER 0      /* ... some explanations and the rationale of    */
#define REDUCE_CODE_SIZE 1  /* ... these three macros                        */

/** state_t represents rijndael state matrix. sincefixed-size memory blocks have
 * an essential role in all algorithms, they are indicated by a specific type */
typedef uint8_t state_t[Nb][4];
typedef uint8_t block_t[BLOCKSIZE];

/** these types are function pointers, whose arguments are fixed-size blocks: */
typedef void (*fdouble_t)(block_t);
typedef void (*fmix_t)(const block_t, block_t);

#if SMALL_CIPHER
typedef unsigned char count_t;
#else
typedef size_t count_t;
#endif

/*----------------------------------------------------------------------------*\
                               Private variables:
\*----------------------------------------------------------------------------*/

/** The array that stores the round keys during AES key-expansion process ... */
static uint8_t RoundKey[BLOCKSIZE * ROUNDS + KEYSIZE];

/** Lookup-tables are static constant, so that they can be placed in read-only
 * storage instead of RAM. They can be computed dynamically trading ROM for RAM.
 * This may be useful in (embedded) bootloader applications, where ROM is often
 * limited. Please refer to:   https://en.wikipedia.org/wiki/Rijndael_S-box   */
static const uint8_t sbox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b,
    0xfe, 0xd7, 0xab, 0x76, 0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0,
    0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0, 0xb7, 0xfd, 0x93, 0x26,
    0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2,
    0xeb, 0x27, 0xb2, 0x75, 0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0,
    0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84, 0x53, 0xd1, 0x00, 0xed,
    0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f,
    0x50, 0x3c, 0x9f, 0xa8, 0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5,
    0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2, 0xcd, 0x0c, 0x13, 0xec,
    0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14,
    0xde, 0x5e, 0x0b, 0xdb, 0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c,
    0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79, 0xe7, 0xc8, 0x37, 0x6d,
    0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f,
    0x4b, 0xbd, 0x8b, 0x8a, 0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e,
    0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e, 0xe1, 0xf8, 0x98, 0x11,
    0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f,
    0xb0, 0x54, 0xbb, 0x16};

#if DECRYPTION
static const uint8_t rsbox[256] = {
    0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38, 0xbf, 0x40, 0xa3, 0x9e,
    0x81, 0xf3, 0xd7, 0xfb, 0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87,
    0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb, 0x54, 0x7b, 0x94, 0x32,
    0xa6, 0xc2, 0x23, 0x3d, 0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e,
    0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2, 0x76, 0x5b, 0xa2, 0x49,
    0x6d, 0x8b, 0xd1, 0x25, 0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16,
    0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92, 0x6c, 0x70, 0x48, 0x50,
    0xfd, 0xed, 0xb9, 0xda, 0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84,
    0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a, 0xf7, 0xe4, 0x58, 0x05,
    0xb8, 0xb3, 0x45, 0x06, 0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02,
    0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b, 0x3a, 0x91, 0x11, 0x41,
    0x4f, 0x67, 0xdc, 0xea, 0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73,
    0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85, 0xe2, 0xf9, 0x37, 0xe8,
    0x1c, 0x75, 0xdf, 0x6e, 0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89,
    0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b, 0xfc, 0x56, 0x3e, 0x4b,
    0xc6, 0xd2, 0x79, 0x20, 0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4,
    0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31, 0xb1, 0x12, 0x10, 0x59,
    0x27, 0x80, 0xec, 0x5f, 0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d,
    0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef, 0xa0, 0xe0, 0x3b, 0x4d,
    0xae, 0x2a, 0xf5, 0xb0, 0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61,
    0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26, 0xe1, 0x69, 0x14, 0x63,
    0x55, 0x21, 0x0c, 0x7d};
#endif

/*----------------------------------------------------------------------------*\
                 Auxiliary functions for the Rijndael algorithm
\*----------------------------------------------------------------------------*/

#define getSBoxValue(num) (sbox[(num)])
#define getSBoxInvert(num) (rsbox[num])

#if REDUCE_CODE_SIZE

/** this function carries out XOR operation on two 128-bit blocks ........... */
static void xorBlock(const block_t src, block_t dest)
{
    uint8_t i;
    for (i = 0; i < BLOCKSIZE; ++i)
        dest[i] ^= src[i];
}

/** doubling in GF(2^8): left-shift and if carry bit is set, xor it with 0x1b */
static uint8_t xtime(uint8_t x)
{
    return (x > 0x7F) * 0x1b ^ (x << 1);
}

#if DECRYPTION

/** This function multiplies two numbers in the Galois field GF(2^8) ........ */
static uint8_t mulGF8(uint8_t x, uint8_t y)
{
    uint8_t m;
    for (m = 0; x > 1; x >>= 1) /* optimized algorithm for nonzero x */
    {
        m ^= (x & 1) * y;
        y = xtime(y);
    }
    return m ^ y; /* or use (9 11 13 14) lookup tables */
}
#endif

#else
#define xtime(y) (y & 0x80 ? (y) << 1 ^ 0x1b : (y) << 1)

#define mulGF8(x, y)                             \
    (((x & 1) * y) ^ ((x >> 1 & 1) * xtime(y)) ^ \
     ((x >> 2 & 1) * xtime(xtime(y))) ^          \
     ((x >> 3 & 1) * xtime(xtime(xtime(y)))))

static void xorBlock(const block_t src, block_t dest)
{
    long long *d = (void *) dest; /* not supported in ANSI-C / ISO-C90 */
    long long const *s = (const void *) src;
    d[0] ^= s[0];
    d[1] ^= s[1];
}
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

    for (i = KEYSIZE; i < (ROUNDS + 1) * BLOCKSIZE; ++i) {
        switch (i % KEYSIZE) {
        case 0:
            memcpy(&RoundKey[i], &RoundKey[i - KEYSIZE], KEYSIZE);
#if Nk == 4
            if (rcon == 0)
                rcon = 0x1b; /* RCON may reach 0 only in AES-128. */
#endif
            RoundKey[i] ^= getSBoxValue(RoundKey[i - 3]) ^ rcon;
            rcon <<= 1;
            break;
        case 1:
        case 2:
            RoundKey[i] ^= getSBoxValue(RoundKey[i - 3]);
            break;
        case 3:
            RoundKey[i] ^= getSBoxValue(RoundKey[i - 7]);
            break;
#if Nk == 8      /* additional round only for AES-256 */
        case 16: /*  0 <= i % KEYSIZE - BLOCKSIZE < 4 */
        case 17:
        case 18:
        case 19:
            RoundKey[i] ^= getSBoxValue(RoundKey[i - 4]);
            break;
#endif
        default:
            RoundKey[i] ^= RoundKey[i - 4];
            break;
        }
    }
}

/** This function adds the round key to the state matrix via an XOR function. */
static void AddRoundKey(const uint8_t round, block_t state)
{
    xorBlock(RoundKey + BLOCKSIZE * round, state);
}

/** Substitute values in the state matrix with associated values in the S-box */
static void SubBytes(block_t state)
{
    uint8_t i;
    for (i = 0; i < BLOCKSIZE; ++i) {
        state[i] = getSBoxValue(state[i]);
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

/** This function mixes the columns of the state matrix in a rotational way.. */
static void MixColumns(state_t *state)
{
    uint8_t a, b, c, d, i;
    for (i = 0; i < Nb; ++i) {
        a = (*state)[i][0] ^ (*state)[i][1];
        b = (*state)[i][1] ^ (*state)[i][2];
        c = (*state)[i][2] ^ (*state)[i][3];

        d = a ^ c; /* d is XOR of all the elements in a row  */
        (*state)[i][0] ^= d ^ xtime(a);
        (*state)[i][1] ^= d ^ xtime(b);

        b ^= d; /* -> b = (*state)[i][3] ^ (*state)[i][0] */
        (*state)[i][2] ^= d ^ xtime(c);
        (*state)[i][3] ^= d ^ xtime(b);
    }
}

/** Encrypts a plain-text input block, into a cipher text block as output ... */
static void rijndaelEncrypt(const block_t input, block_t output)
{
    uint8_t round = ROUNDS;

    /* copy the input to the state matrix, and beware of undefined behavior.. */
    if (input != output)
        memcpy(output, input, BLOCKSIZE);

    AddRoundKey(0, output); /*  Add the first round key to the state  */

    /* The encryption is carried out in #ROUNDS iterations, of which the first
     * #ROUNDS-1 are identical. The last round doesn't involve mixing columns */
    while (round) {
        SubBytes(output);
        ShiftRows((state_t *) output);
        if (--round)
            MixColumns((state_t *) output);
        AddRoundKey(ROUNDS - round, output);
    }
}

/*----------------------------------------------------------------------------*\
                Block-decryption part of the Rijndael algorithm
\*----------------------------------------------------------------------------*/

#if IMPLEMENT(DECRYPTION)

/** Substitutes the values in the state matrix with values of inverted S-box. */
static void InvSubBytes(block_t state)
{
    uint8_t i;
    for (i = 0; i < BLOCKSIZE; ++i) {
        state[i] = getSBoxInvert(state[i]);
    }
}

/** This function shifts/rotates the rows of the state matrix to right ...... */
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
    uint8_t a, b, c, d, i;
    for (i = 0; i < Nb; ++i) { /*  see: crypto.stackexchange.com/q/48872 */
        a = (*state)[i][0];
        b = (*state)[i][1];
        c = (*state)[i][2];
        d = (*state)[i][3];

        (*state)[i][0] =
            mulGF8(14, a) ^ mulGF8(11, b) ^ mulGF8(13, c) ^ mulGF8(9, d);
        (*state)[i][1] =
            mulGF8(14, b) ^ mulGF8(11, c) ^ mulGF8(13, d) ^ mulGF8(9, a);
        (*state)[i][2] =
            mulGF8(14, c) ^ mulGF8(11, d) ^ mulGF8(13, a) ^ mulGF8(9, b);
        (*state)[i][3] =
            mulGF8(14, d) ^ mulGF8(11, a) ^ mulGF8(13, b) ^ mulGF8(9, c);
    }
}

/** Decrypts a cipher-text input block, into a 128-bit plain text as output.. */
static void rijndaelDecrypt(const block_t input, block_t output)
{
    uint8_t round = ROUNDS;

    /* copy the input into state matrix, i.e. state is initialized by input.. */
    if (input != output)
        memcpy(output, input, BLOCKSIZE);

    AddRoundKey(ROUNDS, output); /* First, add the last round key to state */

    /* The decryption completes after #ROUNDS iterations, of which the first
     * #ROUNDS-1 are identical. The last round doesn't involve mixing columns */
    while (round) {
        InvShiftRows((state_t *) output);
        InvSubBytes(output);
        AddRoundKey(--round, output);
        if (round)
            InvMixColumns((state_t *) output);
    }
}
#endif /* DECRYPTION */


#if M_RIJNDAEL
/**
 * @brief   encrypt or decrypt a single block with a given key
 * @param   key       a byte array with a fixed size specified by KEYSIZE
 * @param   mode      mode of operation: 'E' (1) to encrypt, 'D' (0) to decrypt
 * @param   x         input byte array with BLOCKSIZE bytes
 * @param   y         output byte array with BLOCKSIZE bytes
 */
void AES_Cipher(const uint8_t *key, const char mode, const block_t x, block_t y)
{
    fmix_t cipher = mode & 1 ? &rijndaelEncrypt : &rijndaelDecrypt;
    KeyExpansion(key);
    cipher(x, y);
}
#endif

/*----------------------------------------------------------------------------*\
 *              Implementation of different block ciphers modes               *
 *                            Auxiliary Functions                             *
\*----------------------------------------------------------------------------*/

#define AES_SetKey(key) KeyExpansion(key)

#if INCREASE_SECURITY
#define BURN(key) memset(key, 0, sizeof key)
#define SABOTAGE(buf, len) memset(buf, 0, len)
#define MISMATCH constmemcmp /*  constant-time comparison */
#else
#define MISMATCH memcmp
#define SABOTAGE(buf, len) (void) buf
#define BURN(key) (void) key
#endif

#if SMALL_CIPHER
#define putValueB(block, pos, val) \
    block[pos - 1] = val >> 8;     \
    block[pos] = val
#define putValueL(block, pos, val) \
    block[pos + 1] = val >> 8;     \
    block[pos] = val
#define xorWith(block, pos, val) block[pos] ^= (val)
#define incBlock(block, big) ++block[big ? LAST : 0]
#else

#if CTR || FPE

/** copy big endian value to the block, starting at the specified position... */
static void putValueB(block_t block, uint8_t pos, size_t val)
{
    do
        block[pos--] = (uint8_t) val;
    while (val >>= 8);
}
#endif

#if XTS || GCM_SIV

/** copy little endian value to the block, starting at the specified position */
static void putValueL(block_t block, uint8_t pos, size_t val)
{
    do
        block[pos++] = (uint8_t) val;
    while (val >>= 8);
}
#endif

#if KWA || FPE

/** xor a block with a big-endian value, whose LSB is at the specified pos... */
static void xorWith(uint8_t *block, uint8_t pos, size_t val)
{
    do
        block[pos--] ^= (uint8_t) val;
    while (val >>= 8);
}
#endif

#if CTR

/** increment the value of a counter block, regarding its endian-ness ....... */
static void incBlock(block_t block, const char big)
{
    uint8_t i;
    if (big) /*  big-endian counter       */
    {
        for (i = LAST; !++block[i] && i--;)
            ; /*  (inc until no overflow)  */
    } else {
        for (i = 0; !++block[i] && i < 4; ++i)
            ;
    }
}
#endif
#endif /* SMALL CIPHER */

#ifdef AES_PADDING

/** in ECB or CBC without CTS, the last (partial) block has to be padded .... */
static char padBlock(const uint8_t len, block_t block)
{
#if AES_PADDING == 2
    memset(block + len, 0, BLOCKSIZE - len); /*   ISO/IEC 7816-4 padding  */
    block[len] = 0x80;
#elif AES_PADDING
    uint8_t p = BLOCKSIZE - len;            /*   PKCS#7 padding          */
    memset(block + len, p, p);
#else
    if (len == 0)
        return 0; /*   default padding         */
    memset(block + len, 0, BLOCKSIZE - len);
#endif
    return 1;
}
#endif /* PADDING */

#if CBC || CFB || OFB || CTR || OCB

/** Result of applying a function to block `b` is xor-ed with `x` to get `y`. */
static void mixThenXor(const block_t b,
                       fmix_t mix,
                       block_t tmp,
                       const uint8_t *x,
                       const uint8_t len,
                       uint8_t *y)
{
    uint8_t i;
    if (len == 0)
        return; /* f(B) = temp; Y = temp ^ X */

    mix(b, tmp);
    for (i = 0; i < len; ++i)
        y[i] = tmp[i] ^ x[i];
}
#endif

#if EAX && !EAXP || SIV || OCB || CMAC

/** Multiply a block by two in Galois bit field GF(2^128): big-endian version */
static void doubleGF128B(block_t block)
{
    int i, s = 0;
    for (i = BLOCKSIZE; i; s >>= 8) /* loop from last to first,  */
    {                               /* left-shift each byte and  */
        s |= block[--i] << 1;       /* ..add the previous MSB.   */
        block[i] = (uint8_t) s;
    } /* if first MSB is carried:  */
    if (s)
        block[LAST] ^= 0x87; /*   B ^= 10000111b (B.E.)   */
}
#endif

#if XTS || EAXP

/** Multiply a block by two in GF(2^128) field: this is little-endian version */
static void doubleGF128L(block_t block)
{
    int s = 0, i;
    for (i = 0; i < BLOCKSIZE; s >>= 8) /* the same as doubleGF128B  */
    {                                   /* ..but with reversed bytes */
        s |= block[i] << 1;
        block[i++] = (uint8_t) s;
    }
    if (s)
        block[0] ^= 0x87; /*   B ^= 10000111b (L.E.)   */
}
#endif

#if GCM

/** Divide a block by two in GF(2^128) field: used in big endian, 128bit mul. */
static void halveGF128B(block_t block)
{
    unsigned i, t = 0;
    for (i = 0; i < BLOCKSIZE; t <<= 8) /* loop first to last byte,  */
    {                                   /* add the previous LSB then */
        t |= block[i];                  /* ..shift it to the right.  */
        block[i++] = (uint8_t) (t >> 1);
    } /* if block is odd (LSB = 1) */
    if (t & 0x100)
        block[0] ^= 0xe1; /* .. B ^= 11100001b << 120  */
}

/** This function carries out multiplication in 128bit Galois field GF(2^128) */
static void mulGF128(const block_t x, block_t y)
{
    uint8_t i, j, result[BLOCKSIZE] = {0}; /*  working memory           */

    for (i = 0; i < BLOCKSIZE; ++i) {
        for (j = 0; j < 8; ++j) /*  check all the bits of X, */
        {
            if (x[i] << j & 0x80) /*  ..and if any bit is set  */
            {
                xorBlock(y, result); /*  M ^= Y                   */
            }
            halveGF128B(y); /*  Y_next = (Y / 2) in GF   */
        }
    }
    memcpy(y, result, sizeof result); /*  result is saved into y   */
}
#endif /* GCM */

#if GCM_SIV

/** Divide a block by two in GF(2^128) field: the little-endian version (duh) */
static void halveGF128L(block_t block)
{
    unsigned t = 0, i;
    for (i = BLOCKSIZE; i; t <<= 8) /* the same as halveGF128B ↑ */
    {                               /* ..but with reversed bytes */
        t |= block[--i];
        block[i] = (uint8_t) (t >> 1);
    }
    if (t & 0x100)
        block[LAST] ^= 0xe1; /* B ^= L.E 11100001b << 120 */
}

/** Dot multiplication in GF(2^128) field: used in POLYVAL hash for GCM-SIV.. */
static void dotGF128(const block_t x, block_t y)
{
    uint8_t i, j, result[BLOCKSIZE] = {0};

    for (i = BLOCKSIZE; i--;) {
        for (j = 8; j--;) /*  pretty much the same as  */
        {                 /*  ..(reversed) mulGF128    */
            halveGF128L(y);
            if (x[i] >> j & 1) {
                xorBlock(y, result);
            }
        }
    }
    memcpy(y, result, sizeof result); /*  result is saved into y   */
}
#endif /* GCM-SIV */

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
    if (n = dataSize % BLOCKSIZE) {
        while (n--)
            result[n] ^= x[n];
        mix(seed, result);
    }
}
#endif

#if CMAC || SIV || EAX

/** calculate the CMAC hash of input data using pre-calculated keys: D and Q. */
static void cMac(const block_t D,
                 const block_t Q,
                 const void *data,
                 const size_t dataSize,
                 block_t mac)
{
    block_t M = {0};
    uint8_t r = dataSize ? (dataSize - 1) % BLOCKSIZE + 1 : 0;
    const void *endblock = (const char *) data + dataSize - r;

    if (r < sizeof M)
        M[r] = 0x80;
    memcpy(M, endblock, r);            /*  copy last block into M   */
    xorBlock(r < sizeof M ? Q : D, M); /*  ..and pad( M; D, Q )     */

    xMac(data, dataSize - r, mac, &rijndaelEncrypt, mac);
    xMac(M, sizeof M, mac, &rijndaelEncrypt, mac);
}

/** calculate key-dependent constants D and Q for CMAC, regarding endianness: */
static void getSubkeys(const uint8_t *key, fdouble_t dou, block_t D, block_t Q)
{
    AES_SetKey(key);
    rijndaelEncrypt(D, D); /*  H or L_* = Enc(zeros)    */
    dou(D);                /*  D or L_$ = double(L_*)   */
    memcpy(Q, D, BLOCKSIZE);
    dou(Q); /*  Q or L_0 = double(L_$)   */
}
#endif

#if AEAD_MODES && INCREASE_SECURITY

/** for constant-time comparison of memory blocks, to avoid timing attacks:   */
static uint8_t constmemcmp(const uint8_t *src, const uint8_t *dst, uint8_t n)
{
    uint8_t cmp = 0;
    while (n--)
        cmp |= src[n] ^ dst[n];
    return cmp;
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
    memcpy(pntxt, crtxt, crtxtLen);

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
    block_t last = {0};

    if (!n)
        return ENCRYPTION_FAILURE;
    r += (r == 0 && n > 1) * BLOCKSIZE;
    n -= (r == BLOCKSIZE);
    memcpy(last, pntxt + n * BLOCKSIZE, r); /*  hold the last block      */
#endif
    memcpy(crtxt, pntxt, ptextLen); /*  do in-place encryption   */

    AES_SetKey(key);
    for (y = crtxt; n--; y += BLOCKSIZE) {
        xorBlock(iv, y);       /*  Y = P because of memcpy  */
        rijndaelEncrypt(y, y); /*  C = Enc(IV ^ P)          */
        iv = y;                /*  IV_next = C              */
    }
#if CTS /*  cipher-text stealing CS3 */
    if (r) {
        y -= BLOCKSIZE;              /*  or let  y = iv;          */
        memcpy(y + BLOCKSIZE, y, r); /*  'steal' the cipher-text  */
        xorBlock(last, y);           /*  ..to fill the last block */
#else
    if (padBlock(r, y)) {
        xorBlock(iv, y);
#endif
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
    if (!n)
        return DECRYPTION_FAILURE;
    r += (r == 0 && n > 1) * BLOCKSIZE;
    n -= (r == BLOCKSIZE) + (r != 0); /*  last two blocks swapped  */
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
    }        /*  r = 0 unless CTS enabled */
    if (r) { /*  P2 =  Dec(C1) ^ C2       */
        mixThenXor(x, &rijndaelDecrypt, y, x + BLOCKSIZE, r, y + BLOCKSIZE);
        memcpy(y, x + BLOCKSIZE, r);
        rijndaelDecrypt(y, y); /*  copy C2 to Dec(C1): -> T */
        xorBlock(iv, y);       /*  P1 =  IV ^ Dec(T)        */
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

    mixThenXor(iv, &rijndaelEncrypt, tmp, x, dataSize % BLOCKSIZE, y);
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
    block_t iv;
    uint8_t *y;
    count_t n = ptextLen / BLOCKSIZE; /*  number of full blocks    */
    memcpy(crtxt, pntxt, ptextLen);   /*  copy plaintext to output */
    memcpy(iv, iVec, sizeof iv);

    AES_SetKey(key);
    for (y = crtxt; n--; y += BLOCKSIZE) {
        rijndaelEncrypt(iv, iv); /*  C = Enc(IV) ^ P          */
        xorBlock(iv, y);         /*  IV_next = Enc(IV)        */
    }

    mixThenXor(iv, &rijndaelEncrypt, iv, y, ptextLen % BLOCKSIZE, y);
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
 * @brief   the overall scheme of operation in block-counter mode
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
    mixThenXor(c, &rijndaelEncrypt, c, y, dataSize % BLOCKSIZE, y);
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
    putValueB(CTRBLOCK, LAST, CTR_STARTVALUE);
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
 * @param   keypair   pair of encryption keys, each one has KEYSIZE bytes
 * @param   cipher    block cipher function: rijndaelEncrypt or rijndaelDecrypt
 * @param   dataSize  size of input data, to be encrypted/decrypted
 * @param   scid      sector id: if the given value is -1, use tweak value
 * @param   tweakid   data unit identifier, similar to nonce in CTR mode
 * @param   T         one-time pad which is xor-ed with both plain/cipher text
 * @param   storage   working memory; result of encryption/decryption process
 */
static void XEX_Cipher(const uint8_t *keypair,
                       fmix_t cipher,
                       const size_t dataSize,
                       const size_t scid,
                       const block_t tweakid,
                       block_t T,
                       void *storage)
{
    uint8_t *y;
    count_t n = dataSize / BLOCKSIZE;

    if (scid == (size_t) ~0) {         /* the `i` block is either   */
        memcpy(T, tweakid, BLOCKSIZE); /* ..a little-endian number  */
    }                                  /* ..or a byte array.        */
    else {
        putValueL(T, 0, scid);
    }
    AES_SetKey(keypair + KEYSIZE); /* T = encrypt `i` with key2 */
    rijndaelEncrypt(T, T);

    AES_SetKey(keypair); /* key1 is set as cipher key */
    for (y = storage; n--; y += BLOCKSIZE) {
        xorBlock(T, y); /*  xor T with input         */
        cipher(y, y);
        xorBlock(T, y);  /*  Y = T ^ Cipher( T ^ X )  */
        doubleGF128L(T); /*  T_next = T * alpha       */
    }
}

/**
 * @brief   encrypt the input plaintext using XTS-AES block-cipher method
 * @param   keys      two-part encryption key with a fixed size of 2*KEYSIZE
 * @param   twkId     tweak value of data unit, a.k.a sector ID (little-endian)
 * @param   pntxt     input plaintext buffer
 * @param   ptextLen  size of plaintext in bytes
 * @param   crtxt     resulting cipher-text buffer
 */
char AES_XTS_encrypt(const uint8_t *keys,
                     const uint8_t *twkId,
                     const uint8_t *pntxt,
                     const size_t ptextLen,
                     uint8_t *crtxt)
{
    block_t T = {0};
    uint8_t r = ptextLen % BLOCKSIZE, *c;
    size_t len = ptextLen - r;

    if (len == 0)
        return ENCRYPTION_FAILURE;
    memcpy(crtxt, pntxt, len); /* copy input data to output */

    XEX_Cipher(keys, &rijndaelEncrypt, len, ~0, twkId, T, crtxt);
    if (r) { /*  XTS for partial block    */
        c = crtxt + len - BLOCKSIZE;
        memcpy(crtxt + len, c, r); /* 'steal' the cipher-text   */
        memcpy(c, pntxt + len, r); /*  ..for the partial block  */
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
 * @param   twkId     tweak value of data unit, a.k.a sector ID (little-endian)
 * @param   crtxt     input ciphertext buffer
 * @param   crtxtLen  size of ciphertext in bytes
 * @param   pntxt     resulting plaintext buffer
 */
char AES_XTS_decrypt(const uint8_t *keys,
                     const uint8_t *twkId,
                     const uint8_t *crtxt,
                     const size_t crtxtLen,
                     uint8_t *pntxt)
{
    block_t TT, T = {0};
    uint8_t r = crtxtLen % BLOCKSIZE, *p;
    size_t len = crtxtLen - r;

    if (len == 0)
        return DECRYPTION_FAILURE;
    memcpy(pntxt, crtxt, len); /* copy input data to output */
    p = pntxt + len - BLOCKSIZE;

    XEX_Cipher(keys, &rijndaelDecrypt, len - BLOCKSIZE, ~0, twkId, T, pntxt);
    if (r) {
        memcpy(TT, T, sizeof T);
        doubleGF128L(TT);      /*  TT = T * alpha,          */
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
 * @brief   derive the AES-CMAC hash of input data using an encryption key
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
    memset(mac, 0, BLOCKSIZE);
    getSubkeys(key, &doubleGF128B, K1, K2);
    cMac(K1, K2, data, dataSize, mac);
    BURN(RoundKey);
}
#endif /* CMAC */


/*----------------------------------------------------------------------------*\
    GCM-AES (Galois counter mode): authentication with GMAC & main functions
\*----------------------------------------------------------------------------*/
#if IMPLEMENT(GCM)

/** calculates GMAC hash of ciphertext and AAD using authentication subkey H: */
static void GHash(const block_t H,
                  const void *aData,
                  const void *crtxt,
                  const size_t adataLen,
                  const size_t crtxtLen,
                  block_t gsh)
{
    block_t buf = {0}; /*  save bit-sizes into buf  */
    putValueB(buf, BLOCKSIZE - 9, adataLen * 8);
    putValueB(buf, BLOCKSIZE - 1, crtxtLen * 8);

    xMac(aData, adataLen, H, &mulGF128, gsh); /*  first digest AAD, then   */
    xMac(crtxt, crtxtLen, H, &mulGF128, gsh); /*  ..ciphertext, and then   */
    xMac(buf, sizeof buf, H, &mulGF128, gsh); /*  ..bit sizes into GHash   */
}

/** encrypt zeros to get authentication subkey H, and prepare the IV for GCM. */
static void GInitialize(const uint8_t *key,
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
    GInitialize(key, nonce, H, iv); /*  get IV & auth. subkey H  */

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
    GInitialize(key, nonce, H, iv);
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
    uint8_t p, s = BLOCKSIZE - 2;
    memcpy(M, iv, BLOCKSIZE); /*  initialize CBC-MAC       */

    M[0] |= (CCM_TAG_LEN - 2) << 2; /*  set some flags on M_*    */
    putValueB(M, LAST, ptextLen);   /*  copy data size into M_*  */
    if (aDataLen)                   /*  feed aData into CBC-MAC  */
    {
        if (aDataLen < s)
            s = aDataLen;
        p = aDataLen < 0xFF00 ? 1 : 5;
        putValueB(A, p, aDataLen); /*  copy aDataLen into A     */
        if (p == 5) {
            s -= 4;
            A[0] = 0xFF;
            A[1] = 0xFE; /*  prepend FFFE to aDataLen */
        }
        memcpy(A + p + 1, aData, s); /*  append ADATA             */
        M[0] |= 0x40;
        rijndaelEncrypt(M, M); /*  flag M_* and encrypt it  */
    }

    xMac(A, sizeof A, M, &rijndaelEncrypt, M); /*  CBC-MAC start of aData   */
    if (aDataLen > s)                          /*  CBC-MAC rest of aData    */
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
                block_t V)
{
    block_t T = {0}, D = {0}, Q;
    uint8_t r = ptextLen >= BLOCKSIZE ? BLOCKSIZE : ptextLen % BLOCKSIZE;
    uint8_t const *x = (uint8_t const *) pntxt - r + ptextLen;

    getSubkeys(key, &doubleGF128B, D, Q);
    cMac(D, Q, T, sizeof T, T); /*  T_0 = CMAC(zero block)   */
    if (aDataLen)               /*  process each ADATA unit  */
    {                           /*  ..the same way as this:  */
        doubleGF128B(T);
        cMac(D, Q, aData, aDataLen, V); /*  C_A = CMAC(ADATA)        */
        xorBlock(V, T);                 /*  T_1 = double(T_0) ^ C_A  */
        memset(V, 0, BLOCKSIZE);
    }
    if (r < sizeof T) {
        doubleGF128B(T);
        T[r] ^= 0x80; /*  T = double(T_n) ^ pad(X) */
        while (r--)
            T[r] ^= x[r];
    } else
        xorBlock(x, T); /*  T = T_n  xor_end  X      */

    cMac(D, Q, T, sizeof T, V); /*  I.V = CMAC*(T)           */
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
    block_t IV = {0};
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
    memset(IV, 0, sizeof IV);
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
    block_t buf = {0}; /*  save bit-sizes into buf  */
    putValueL(buf, 0, aDataLen * 8);
    putValueL(buf, 8, ptextLen * 8);

    xMac(aData, aDataLen, H, &dotGF128, pv); /*  first digest AAD, then   */
    xMac(pntxt, ptextLen, H, &dotGF128, pv); /*  ..plaintext, and then    */
    xMac(buf, sizeof buf, H, &dotGF128, pv); /*  ..bit sizes into POLYVAL */
}

/** derive the pair of authentication-encryption-keys from main key and nonce */
static void DeriveGSKeys(const uint8_t *key, const uint8_t *nonce, block_t AK)
{
    uint8_t AEKeypair[KEYSIZE + 24];
    uint8_t iv[BLOCKSIZE], *k = AEKeypair;
    memcpy(iv + 4, nonce, 12);

    AES_SetKey(key);
    for (*(int32_t *) iv = 0; *iv < KEYSIZE / 8 + 2; ++*iv) {
        rijndaelEncrypt(iv, k); /* encrypt & take half, then */
        k += 8;                 /* ..increment iv's LSB      */
    }
    AES_SetKey(AEKeypair + BLOCKSIZE); /*  set the main cipher-key  */
    memcpy(AK, AEKeypair, BLOCKSIZE);  /*  take authentication key  */
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
    DeriveGSKeys(key, nonce, H); /* get authentication subkey */

    Polyval(H, aData, pntxt, aDataLen, ptextLen, S);
    for (*H = 0; *H < 12; ++*H) { /* using H[0] as counter!    */
        S[*H] ^= nonce[*H];       /* xor nonce with POLYVAL    */
    }
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

    DeriveGSKeys(key, nonce, H);           /* get authentication subkey */
    memcpy(S, crtxt + crtxtLen, sizeof S); /* tag is IV for CTR cipher  */
    S[LAST] |= 0x80;
    CTR_Cipher(S, 0, crtxt, crtxtLen, pntxt);

    memset(S, 0, sizeof S);
    Polyval(H, aData, pntxt, aDataLen, crtxtLen, S);
    for (*H = 0; *H < 12; ++*H) { /* using H[0] as counter!    */
        S[*H] ^= nonce[*H];       /* xor nonce with POLYVAL    */
    }
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

/** this function calculates the OMAC hash of a data array using D (K1) and Q */
static void OMac(const uint8_t t,
                 const block_t D,
                 const block_t Q,
                 const void *data,
                 const size_t dataSize,
                 block_t mac)
{
    block_t M = {0};
#if EAXP
    memcpy(mac, t ? (dataSize ? Q : M) : D, sizeof M);
    if (dataSize || !t) /*   ignore null ciphertext  */
#else
    if (dataSize == 0) {
        memcpy(M, D, sizeof M); /*  OMAC = Enc( D ^ [t]_n )  */
    }
    M[LAST] ^= t; /*  else: C1 = Enc( [t]_n )  */
    rijndaelEncrypt(M, mac);
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
#define fDouble doubleGF128L
#else
                     const uint8_t *aData,
                     const size_t aDataLen,
                     uint8_t *crtxt,
                     uint8_t *auTag)
#define fDouble doubleGF128B
#define nonceLen EAX_NONCE_LEN
#endif
{
    block_t D = {0}, Q, mac;
    getSubkeys(key, &fDouble, D, Q);
    OMac(0, D, Q, nonce, nonceLen, mac); /*  N = OMAC(0; nonce)       */

#if EAXP
    *(int32_t *) &crtxt[ptextLen] = *(int32_t *) &mac[12];
    mac[12] &= 0x7F;
    mac[14] &= 0x7F; /*  clear 2 bits to get N'   */
    CTR_Cipher(mac, 1, pntxt, ptextLen, crtxt);

    OMac(2, D, Q, crtxt, ptextLen, mac); /*  C' = CMAC'( ciphertext ) */
    *(int32_t *) &crtxt[ptextLen] ^= *(int32_t *) &mac[12];
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
    getSubkeys(key, &fDouble, D, Q);
    OMac(2, D, Q, crtxt, crtxtLen, tag); /*  C = OMAC(2; ciphertext)  */

#if EAXP
    OMac(0, D, Q, nonce, nonceLen, mac); /*  N = CMAC'( nonce )       */
    *(int32_t *) &tag[12] ^= *(int32_t *) &mac[12];
    *(int32_t *) &tag[12] ^= *(int32_t *) &crtxt[crtxtLen];

    mac[12] &= 0x7F; /*  clear 2 bits to get N'   */
    mac[14] &= 0x7F;
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
              OCB-AES (offset codebook mode): auxiliary functions
\*----------------------------------------------------------------------------*/
#if IMPLEMENT(OCB)

/** Get the offset block (Δ_i) which is initialized by Δ_0, at the specified
 * index for a given L$. This method has minimum memory usage, but it is slow */
static void OffsetB(const block_t Ld, const count_t index, block_t delta)
{
    size_t m, b = 1;
    block_t L;
    memcpy(L, Ld, sizeof L); /*  initialize L_$           */

    while (b <= index && b) /*  we can pre-calculate all */
    {                       /*  ..L_{i}s to boost speed  */
        m = (4 * b - 1) & (index - b);
        b <<= 1;         /*  L_0 = double( L_$ )      */
        doubleGF128B(L); /*  L_i = double( L_{i-1} )  */
        if (b > m)
            xorBlock(L, delta); /*  Δ_new = Δ ^ L_i          */
    }
}

/**
 * @brief   encrypt or decrypt a data unit using OCB-AES method
 * @param   nonce     a.k.a initialization vector with fixed size: OCB_NONCE_LEN
 * @param   cipher    block-cipher function: rijndaelEncrypt or rijndaelDecrypt
 * @param   input     input plain/cipher-text buffer
 * @param   dataSize  size of data
 * @param   Ls        L_* is the result of the encryption of a zero block
 * @param   Ld        L_$ = double(L_*) in GF(2^128)
 * @param   Del       Δ_m  a.k.a last offset (sometimes Δ*, which is Δ_m ^ L_*)
 * @param   output    encrypted/decrypted data storage
 */
static void OCB_Cipher(const uint8_t *nonce,
                       fmix_t cipher,
                       const void *input,
                       const size_t dataSize,
                       block_t Ls,
                       block_t Ld,
                       block_t Del,
                       void *output)
{
    uint8_t kt[2 * BLOCKSIZE] = {OCB_TAG_LEN << 4 & 0xFF};
    count_t n = nonce[OCB_NONCE_LEN - 1] & 0x3F, i;
    uint8_t r = n % 8, *y = output;

    memcpy(output, input, dataSize); /* copy input data to output */

    memcpy(kt + BLOCKSIZE - OCB_NONCE_LEN, nonce, OCB_NONCE_LEN);
    kt[LAST - OCB_NONCE_LEN] |= 1;
    kt[LAST] &= 0xC0; /* clear last 6 bits         */
    n /= 8;           /* copy last 6 bits to (n,r) */

    rijndaelEncrypt(kt, kt);           /* construct K_top           */
    memcpy(kt + BLOCKSIZE, kt + 1, 8); /* stretch K_top             */
    xorBlock(kt, kt + BLOCKSIZE);
    for (i = 0; i < BLOCKSIZE; ++n) /* shift the stretched K_top */
    {
        kt[i++] = kt[n] << r | kt[n + 1] >> (8 - r);
    }
    n = dataSize / BLOCKSIZE;
    r = dataSize % BLOCKSIZE;

    rijndaelEncrypt(Ls, Ls); /*  L_* = Enc(zero block)    */
    memcpy(Ld, Ls, BLOCKSIZE);
    doubleGF128B(Ld); /*  L_$ = double(L_*)        */
    if (n == 0)       /*  processed nonce is Δ_0   */
    {
        memcpy(Del, kt, BLOCKSIZE); /*  initialize Δ_0           */
    }
    for (i = 0; i < n; y += BLOCKSIZE) {
        memcpy(Del, kt, BLOCKSIZE); /*  calculate Δ_i using my   */
        OffsetB(Ld, ++i, Del);      /*  .. 'magic' algorithm     */
        xorBlock(Del, y);
        cipher(y, y); /* Y = Δ_i ^ Cipher(Δ_i ^ X) */
        xorBlock(Del, y);
    }
    if (r) /*  Δ_* = Δ_n ^ L_* and then */
    {      /*  Y_* = Enc(Δ_*) ^ X       */
        xorBlock(Ls, Del);
        mixThenXor(Del, &rijndaelEncrypt, kt, y, r, y);
        Del[r] ^= 0x80; /*    pad it for checksum    */
    }
}

static void nop(const block_t x, block_t y) {}

/** derives OCB authentication tag. the first three arguments are pre-calculated
 * namely, Δ_* (or sometimes Δ_m), L_* = encrypt(zeros) and L_$ = double(L_*) */
static void OCB_GetTag(const block_t Ds,
                       const block_t Ls,
                       const block_t Ld,
                       const void *pntxt,
                       const void *aData,
                       const size_t ptextLen,
                       const size_t aDataLen,
                       block_t tag)
{
    uint8_t const r = aDataLen % BLOCKSIZE, *x = aData;
    count_t i, n = aDataLen / BLOCKSIZE;
    block_t S = {0};                      /*  checksum, i.e.           */
    xMac(pntxt, ptextLen, NULL, &nop, S); /*  ..xor of all plaintext   */

    xorBlock(Ds, S);
    xorBlock(Ld, S);
    rijndaelEncrypt(S, tag); /* Tag0 = Enc(L_$ ^ Δ_* ^ S) */
    if (aDataLen == 0)
        return;

    memset(S, 0, sizeof S); /*  PMAC authentication:     */
    for (i = 0; i < n; x += BLOCKSIZE) {
        OffsetB(Ld, ++i, S);
        xorBlock(x, S);
        rijndaelEncrypt(S, S); /*  S_i = Enc(A_i ^ Δ_i)     */
        xorBlock(S, tag);      /*  Tag_{i+1} = Tag_i ^ S_i  */
        memset(S, 0, sizeof S);
    }
    if (r) {
        OffsetB(Ld, n, S);            /*  S = calculated Δ_n       */
        S[r] ^= 0x80;                 /*  A_* = A || 1  (padded)   */
        xMac(x, r, Ls, &xorBlock, S); /*  S_* = A_* ^ L_* ^ Δ_n    */
        rijndaelEncrypt(S, S);
        xorBlock(S, tag); /*  Tag = Enc(S_*) ^ Tag_n   */
    }
}

/*----------------------------------------------------------------------------*\
                 OCB-AES (offset codebook mode): main functions
\*----------------------------------------------------------------------------*/
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
    block_t Ls = {0}, Ld, delta;
    AES_SetKey(key);
    OCB_Cipher(nonce, &rijndaelEncrypt, pntxt, ptextLen, Ls, Ld, delta, crtxt);
    OCB_GetTag(delta, Ls, Ld, pntxt, aData, ptextLen, aDataLen, auTag);
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
    block_t Ls = {0}, Ld, delta;
    if (tagLen && tagLen != OCB_TAG_LEN)
        return DECRYPTION_FAILURE;

    AES_SetKey(key);
    OCB_Cipher(nonce, &rijndaelDecrypt, crtxt, crtxtLen, Ls, Ld, delta, pntxt);
    OCB_GetTag(delta, Ls, Ld, pntxt, aData, crtxtLen, aDataLen, delta);
    BURN(RoundKey); /* tag was saved into delta  */

    if (MISMATCH(delta, crtxt + crtxtLen, tagLen)) {
        SABOTAGE(pntxt, crtxtLen);
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
 * @param   wrapped   wrapped secret. note: size of output = secretLen + HB
 * @return            error if size is not a multiple of HB, or size < BLOCKSIZE
 */
char AES_KEY_wrap(const uint8_t *kek,
                  const uint8_t *secret,
                  const size_t secretLen,
                  uint8_t *wrapped)
{
    uint8_t A[BLOCKSIZE], *r, i;
    count_t n = secretLen / HB, j; /*  number of semi-blocks    */
    if (n < 2 || secretLen % HB)
        return ENCRYPTION_FAILURE;

    memset(A, 0xA6, HB);                     /*  initialization vector    */
    memcpy(wrapped + HB, secret, secretLen); /*  copy input to the output */
    AES_SetKey(kek);

    for (i = 0; i < 6; ++i) {
        r = wrapped;
        for (j = 0; j++ < n;) {
            r += HB;
            memcpy(A + HB, r, HB); /*  C = Enc(A | R[j])        */
            rijndaelEncrypt(A, A); /*  R[j] = LSB(64, C)        */
            memcpy(r, A + HB, HB); /*  A = MSB(64, C) ^ t       */
            xorWith(A, HB - 1, n * i + j);
        }
    }
    memcpy(wrapped, A, HB);

    BURN(RoundKey);
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
    uint8_t A[BLOCKSIZE], *r, i;
    count_t n = wrapLen / HB - 1, j; /*  number of semi-blocks    */
    if (n < 2 || wrapLen % HB)
        return DECRYPTION_FAILURE;

    memcpy(A, wrapped, HB); /*  authentication vector    */
    memcpy(secret, wrapped + HB, wrapLen - HB);
    AES_SetKey(kek);

    for (i = 6; i--;) {
        r = secret + n * HB;
        for (j = n; j; --j) {
            r -= HB;
            xorWith(A, HB - 1, n * i + j);
            memcpy(A + HB, r, HB); /*  D = Dec(A ^ t | R[j])    */
            rijndaelDecrypt(A, A); /*  A = MSB(64, D)           */
            memcpy(r, A + HB, HB); /*  R[j] = LSB(64, D)        */
        }
    }
    while (++i != HB)
        j |= A[i] ^ 0xA6; /*  authenticate/error check */

    BURN(RoundKey);
    return j ? AUTHENTICATION_FAILURE : ENDED_IN_SUCCESS;
}
#endif /* KWA */


/*----------------------------------------------------------------------------*\
     Poly1305-AES message authentication: auxiliary functions and main API
\*----------------------------------------------------------------------------*/
#if IMPLEMENT(POLY1305)
#define Sp (BLOCKSIZE + 1) /* size of poly1305 blocks   */

/** derive modulo(2^130-5) for a little endian block, by repeated subtraction */
static void modLPoly(uint8_t *block, const uint8_t ovrfl)
{
    uint8_t i = BLOCKSIZE, *msb = block + i;
    int n = ovrfl * 0x40 + *msb / 4; /* n = B / (2 ^ 130)         */
    int32_t q = n + *msb == 3 && *block >= 0xFB;

    while (q && --i)
        q = block[i] == 0xFF; /* compare block to 2^130-5  */

    for (n += q; n; n = *msb > 3) /* mod = B - n * (2^130-5)   */
    {
        for (q = 5 * n, i = 0; q && i < Sp; q >>= 8) {
            q += block[i];            /* to get mod, first derive  */
            block[i++] = (uint8_t) q; /* .. B + (5 * n) and then   */
        }                             /* .. subtract n * (2^130)   */
        *msb -= 4 * (uint8_t) n;
    }
}

/** add two little-endian poly1305 blocks. use modular addition if necessary. */
static void addLBlocks(const uint8_t *x, const uint8_t len, uint8_t *y)
{
    int s = 0, i;
    for (i = 0; i < len; s >>= 8) {
        s += x[i] + y[i];
        y[i++] = (uint8_t) s; /*  s >> 8 is overflow/carry */
    }
    if (len == Sp)
        modLPoly(y, (uint8_t) s);
}

/** modular multiplication of a block by 2^s, i.e. left shift block to s bits */
static void shiftLBlock(uint8_t *block, const uint8_t shl)
{
    unsigned i, t = 0;
    for (i = 0; i < Sp; t >>= 8) /*  similar to doubleGF128L  */
    {
        t |= block[i] << shl; /*  shl may vary from 1 to 8 */
        block[i++] = (uint8_t) t;
    }
    modLPoly(block, (uint8_t) t);
}

/** modular multiplication of two little-endian poly1305 blocks. y *= x mod P */
static void mulLBlocks(const uint8_t *x, uint8_t *y)
{
    uint8_t i, b, nz, result[Sp] = {0};

    for (i = Sp; i--; ++x) {
        for (b = nz = 1; b != 0; nz = 1)   /*  check every bit of x[i]  */
        {                                  /*  ..and if any bit was set */
            if (*x & b)                    /*  ..add y to the result.   */
            {                              /*  then, calculate the      */
                addLBlocks(y, Sp, result); /*  ..distance to the next   */
            }                              /*  ..set bit, i.e. nz       */
            for (b <<= 1; (*x & b) < b; b <<= 1)
                ++nz;
            shiftLBlock(y, nz);
        }
    }
    memcpy(y, result, sizeof result); /*  result is saved into y   */
}

/**
 * @brief   derive the Poly1305-AES hash of message using a nonce and key pair.
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
    uint8_t r[Sp], poly[Sp] = {0}, c[Sp] = {0}, rk[Sp] = {1};
    uint8_t s = (dataSize > 0), i;
    uint8_t j = (dataSize - s) % BLOCKSIZE + s;
    count_t q = (dataSize - s) / BLOCKSIZE;
    uint8_t const *ptr = (uint8_t const *) data + q * BLOCKSIZE;

    memcpy(r, keys + KEYSIZE, BLOCKSIZE); /* extract r from (k,r) pair */
    for (r[i = BLOCKSIZE] = 0; i; i -= 4) {
        r[i] &= 0xFC;     /* clear bottom 2 bits       */
        r[i - 1] &= 0x0F; /* clear top 4 bits          */
    }

    for (q += s; q--; ptr -= (j = BLOCKSIZE)) {
        memcpy(c, ptr, j);             /* copy message to chunk     */
        c[j] = 1;                      /* append 1 to each chunk    */
        mulLBlocks(r, rk);             /* r^k = r^{k-1} * r         */
        mulLBlocks(rk, c);             /* calculate c_{q-k} * r^k   */
        addLBlocks(c, sizeof c, poly); /* add to poly (mod 2^130-5) */
    }

    AES_SetKey(keys);
    rijndaelEncrypt(nonce, mac); /* derive AES_k(nonce)       */
    BURN(RoundKey);
    addLBlocks(poly, BLOCKSIZE, mac); /* mac = poly + AES_k(nonce) */
}
#endif /* POLY1305 */


/*----------------------------------------------------------------------------*\
   FPE-AES (format-preserving encryption): definitions & auxiliary functions
\*----------------------------------------------------------------------------*/
#if IMPLEMENT(FPE)

#if !CUSTOM_ALPHABET
#define ALPHABET "0123456789"
#define string_t char *    /*  string pointer type      */
#define RADIX 10           /*  strlen (ALPHABET)        */
#define LOGRDX 3.321928095 /*  log2 (RADIX)             */
#define MINLEN 6           /*  ceil (6 / log10 (RADIX)) */
#define MAXLEN 56          /*  for FF3-1 only           */
#endif

#if RADIX > 0x100
typedef unsigned short rbase_t; /*  num type in base-radix   */
#else
typedef uint8_t rbase_t;
#endif

#if FF_X == 3

/** convert a string in base-RADIX to a little-endian number, denoted by num  */
static void numRadix(const rbase_t *s, uint8_t len, uint8_t *num, uint8_t bytes)
{
    size_t i, d;
    memset(num, 0, bytes);
    while (len--) {
        for (d = s[len], i = 0; i < bytes; d >>= 8) {
            d += num[i] * RADIX; /*  num = num * RADIX + d    */
            num[i++] = (uint8_t) d;
        }
    }
}

/** convert a little-endian number to its base-RADIX representation string: s */
static void strRadix(const uint8_t *num, uint8_t bytes, rbase_t *s, uint8_t len)
{
    size_t i, b;
    memset(s, 0, sizeof(rbase_t) * len);
    while (bytes--) {
        for (b = num[bytes], i = 0; i < len; b /= RADIX) {
            b += s[i] << 8; /*  numstr = numstr << 8 + b */
            s[i++] = b % RADIX;
        }
    }
}

/** add two numbers in base-RADIX represented by q and p, so that p = p + q   */
static void numstrAdd(const rbase_t *q, const uint8_t N, rbase_t *p)
{
    uint8_t a, c = 0, i;
    for (i = 0; i < N; c = a >= RADIX) /* little-endian addition    */
    {
        a = p[i] + q[i] + c;
        p[i++] = (rbase_t) (a % RADIX);
    }
}

/** subtract two numbers in base-RADIX represented by q and p, so that p -= q */
static void numstrSub(const rbase_t *q, const uint8_t N, rbase_t *p)
{
    uint8_t s, c = 0, i;
    for (i = 0; i < N; c = s < RADIX) /* little-endian subtraction */
    {
        s = RADIX + p[i] - q[i] - c;
        p[i++] = (rbase_t) (s % RADIX);
    }
}

/** apply the FF3-1 round at step `i` to the input string X with length `len` */
static void FF3round(const uint8_t i,
                     const uint8_t *T,
                     const uint8_t u,
                     const uint8_t len,
                     rbase_t *X)
{
    uint8_t P[BLOCKSIZE], s = i & 1 ? len : len - u;

    T += (s < len) * 4; /* W=TL if i is odd, else TR */
    P[15] = T[0];
    P[14] = T[1];
    P[13] = T[2];
    P[12] = T[3] ^ i;

    numRadix(X - s, len - u, P, 12); /*  B = *X_end - s           */
    rijndaelEncrypt(P, P);
    strRadix(P, sizeof P, X, u); /*  C = *X = REV( STRm(c) )  */
}

/** encrypt/decrypt a base-RADIX string X with size len using FF3-1 algorithm */
static void FF3_Cipher(const char mode,
                       const uint8_t *key,
                       const uint8_t len,
                       const uint8_t *tweak,
                       rbase_t *X)
{
    rbase_t *Xc = X + len;
    uint8_t T[8], i, *k = (void *) Xc, u = (len + mode) / 2, r = mode ? 0 : 8;

    memcpy(T, tweak, 7);
    T[7] = T[3] << 4 & 0xF0;
    T[3] &= 0xF0;

    /* note that the official test vectors are based on the old version of FF3,
     * which uses a 64-bit tweak. you can uncomment this line to verify them:
    memcpy( T, tweak, 8 );                                                    */

    for (i = KEYSIZE; i--;)
        k[i] = *key++;
    AES_SetKey(k); /*  key is reversed          */

    for (i = r; i < 8; u = len - u) /*  Feistel procedure        */
    {
        FF3round(i++, T, u, len, Xc); /*  encryption rounds        */
        numstrAdd(Xc, u, i & 1 ? X : Xc - u);
    }
    for (i = r; i > 0; u = len - u) /*  A = *X,  B = *(Xc - u)   */
    {
        FF3round(--i, T, u, len, Xc); /*  decryption rounds        */
        numstrSub(Xc, u, i & 1 ? Xc - u : X);
    }
}
#else  /* FF1: */

/** convert a string in base-RADIX to a big-endian number, denoted by num     */
static void numRadix(const rbase_t *s, size_t len, uint8_t *num, size_t bytes)
{
    size_t i, d;
    memset(num, 0, bytes);
    while (len--) {
        for (d = *s++, i = bytes; i; d >>= 8) {
            d += num[--i] * RADIX; /*  num = num * RADIX + d    */
            num[i] = (uint8_t) d;
        }
    }
}

/** convert a big-endian number to its base-RADIX representation string: s    */
static void strRadix(const uint8_t *num, size_t bytes, rbase_t *s, size_t len)
{
    size_t i, b;
    memset(s, 0, sizeof(rbase_t) * len);
    while (bytes--) {
        for (b = *num++, i = len; i; b /= RADIX) {
            b += s[--i] << 8; /*  numstr = numstr << 8 + b */
            s[i] = b % RADIX;
        }
    }
}

/** add two numbers in base-RADIX represented by q and p, so that p = p + q   */
static void numstrAdd(const rbase_t *q, size_t N, rbase_t *p)
{
    size_t a, c;
    for (c = 0; N--; c = a >= RADIX) /*  big-endian addition      */
    {
        a = p[N] + q[N] + c;
        p[N] = a % RADIX;
    }
}

/** subtract two numbers in base-RADIX represented by q and p, so that p -= q */
static void numstrSub(const rbase_t *q, size_t N, rbase_t *p)
{
    size_t s, c;
    for (c = 0; N--; c = s < RADIX) /*  big-endian subtraction   */
    {
        s = RADIX + p[N] - q[N] - c;
        p[N] = s % RADIX;
    }
}

static size_t bf, df; /*  b and d constants in FF1 */

/** apply the FF1 round at step `i` to the input string X with length `len`   */
static void FF1round(const uint8_t i,
                     const block_t P,
                     const size_t u,
                     const size_t len,
                     rbase_t *Xc)
{
    size_t j = bf % BLOCKSIZE, s = i & 1 ? len : len - u;
    block_t R = {0};
    uint8_t *num = (void *) (Xc + u); /* use pre-allocated memory  */

    R[LAST - j] = i;
    numRadix(Xc - s, len - u, num, bf); /* get NUM_radix(B)          */
    memcpy(R + BLOCKSIZE - j, num, j);  /* feed NUMradix(B) into PRF */
    xMac(P, BLOCKSIZE, R, &rijndaelEncrypt, R);
    xMac(num + j, bf - j, R, &rijndaelEncrypt, R);

    j = (df - 1) / BLOCKSIZE; /* R = PRF(P || Q)           */
    memcpy(num, R, sizeof R);
    for (num += j * sizeof R; j; num -= sizeof R) {
        memcpy(num, R, sizeof R);
        xorWith(num, LAST, j--);   /* num = R || R ^ [j] ||...  */
        rijndaelEncrypt(num, num); /* S = R || Enc(R ^ [j])...  */
    }
    strRadix(num, df, Xc, u); /* take first d bytes of S   */
}

/** encrypt/decrypt a base-RADIX string X with length len using FF1 algorithm */
static void FF1_Cipher(const char mode,
                       const uint8_t *key,
                       const size_t len,
                       const uint8_t *tweak,
                       const size_t tweakLen,
                       rbase_t *X)
{
    block_t P = {1, 2, 1, 0, 0, 0, 10};
    rbase_t *Xc = X + len;
    uint8_t i = tweakLen % BLOCKSIZE, r = mode ? 0 : 10;
    size_t u = (len + 1 - mode) >> 1, t = tweakLen - i;

    P[7] = len / 2 & 0xFF;
    putValueB(P, 5, RADIX); /* P = [1,2,1][radix][10]... */
    putValueB(P, 11, len);
    putValueB(P, 15, tweakLen);

    AES_SetKey(key);
    rijndaelEncrypt(P, P);
    xMac(tweak, t, P, &rijndaelEncrypt, P); /* P = PRF(P || tweak)       */
    if (i < BLOCKSIZE - bf % BLOCKSIZE) {
        for (t = tweakLen; i;)
            P[--i] ^= tweak[--t];
    } else /* zero pad and feed to PRF  */
    {
        xMac(&tweak[t], i, P, &rijndaelEncrypt, P);
    }
    for (i = r; i < 10; u = len - u) /* A = *X,  B = *(Xc - u)    */
    {
        FF1round(i++, P, u, len, Xc); /* encryption rounds         */
        numstrAdd(Xc, u, i & 1 ? X : Xc - u);
    }
    for (i = r; i != 0; u = len - u) {
        FF1round(--i, P, u, len, Xc); /* decryption rounds         */
        numstrSub(Xc, u, i & 1 ? Xc - u : X);
    }
}
#endif /* FF_X */

/*----------------------------------------------------------------------------*\
                            FPE-AES: main functions
\*----------------------------------------------------------------------------*/
#include <stdlib.h>

/** allocate the required memory and validate the input string in FPE mode... */
static char FPEsetup(const string_t str, const size_t len, rbase_t **indices)
{
    string_t alpha = ALPHABET;
    size_t i = (len + 1) / 2;
    size_t j = (len + i) * sizeof(rbase_t);

#if FF_X == 3
    if (len > MAXLEN)
        return 'L';
    i *= sizeof(rbase_t);
    j += (i < KEYSIZE) * (KEYSIZE - i);
#else
    bf = (size_t) (LOGRDX * i + 8 - 1e-10) / 8; /*  extra memory is required */
    df = (bf + 7) & ~3UL;                       /*  ..whose size is at least */
    j += (df + 12) & ~15UL;                     /*  ..ceil( d/16 ) blocks    */
#endif
    if (len < MINLEN || (*indices = malloc(j)) == NULL) {
        return 'M'; /*  memory allocation failed */
    }
    for (i = 0; i < len; ++i) {
        for (j = RADIX; --j && alpha[j] != *str;) {
        }
        if (alpha[j] != *str++) {
            free(*indices); /*  invalid character found  */
            return 'I';
        }
        (*indices)[i] = (rbase_t) j;
    }
    return 0;
}

/** make the output string after completing FPE encrypt/decryption procedures */
static void FPEfinalize(const rbase_t *index, size_t len, void **output)
{
    string_t alpha = ALPHABET, *s = *output;
    while (len--)
        *s++ = alpha[*index++];
    *s = 0;
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
    if (FPEsetup(pntxt, ptextLen, &index) != 0)
        return ENCRYPTION_FAILURE;

#if FF_X == 3
    FF3_Cipher(1, key, ptextLen, tweak, index);
#else
    FF1_Cipher(1, key, ptextLen, tweak, tweakLen, index);
#endif
    BURN(RoundKey);
    FPEfinalize(index, ptextLen, &crtxt);
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
    if (FPEsetup(crtxt, crtxtLen, &index) != 0)
        return DECRYPTION_FAILURE;

#if FF_X == 3
    FF3_Cipher(0, key, crtxtLen, tweak, index);
#else
    FF1_Cipher(0, key, crtxtLen, tweak, tweakLen, index);
#endif
    BURN(RoundKey);
    FPEfinalize(index, crtxtLen, &pntxt);
    free(index);
    return ENDED_IN_SUCCESS;
}
#endif /* FPE */

#define TestStringSize 114 /* hex characters in plain-text */
#define BuffL ((TestStringSize / 2 + 31) & ~15)

static const char *
    masterKey =
       "0001020304050607 08090A0B0C0D0E0F 1011121314151617 18191A1B1C1D1E1F",
   *secretKey =
       "0011223344556677 8899AABBCCDDEEFF 0001020304050607 08090A0B0C0D0E0F",
   *cipherKey =
       "279fb74a7572135e 8f9b8ef6d1eee003 69c4e0d86a7b0430 d8cdb78070b4c55a",
   *iVec = "8ea2b7ca516745bf eafc49904b496089",
   *plainText =
       "c9f775baafa36c25 cd610d3c75a482ea dda97ca4864cdfe0 6eaf70a0ec0d7191\
                  d55027cf8f900214 e634412583ff0b47 8ea2b7ca516745bf ea",
#if AES_KEY_LENGTH == 16
   *ecbcipher =
       "5d00c273f8b2607d a834632dcbb521f4 697dd4ab20bb0645 32a6545e24e33ae9\
                  f545176111f93773 dbecd262841cf83b 10d145e71b772cf7 a12889cda84be795",
#if !CTS /* ↑↑ and ↓↓ both zero-padded plain text. */
   *cbccipher =
       "65c48fdf9fbd6261 28f2d8bac3f71251 75e7f4821fda0263 70011632779d7403\
                  7e9e2d298e154bc4 2dc7a9bc419b915d c119ef461ac4e1bc 8a7e36bf92b3b3d1",
#else
   *cbccipher =
       "65c48fdf9fbd6261 28f2d8bac3f71251 75e7f4821fda0263 70011632779d7403\
                  c119ef461ac4e1bc 8a7e36bf92b3b3d1 7e9e2d298e154bc4 2d",
#endif
   *cfbcipher =
       "edab3105e673bc9e b9102539a9f457bc 245c14e1bff81b5b 4a4a147c988cb0a6\
                  3f9c56525efbe64a 876ad1d761d3fc93 59fb4f5b2354acd4 90",
   *ofbcipher =
       "edab3105e673bc9e b9102539a9f457bc d28c8e4c92995f5c d9426926be1e775d\
                  e22b8ce4d0278b18 181b8bec93b9726f 959aa5d701d46102 f0",
#if CTR_IV_LENGTH == 16
   *ctrcipher =
       "edab3105e673bc9e b9102539a9f457bc f2e2606dfa3f93c5 c51b910a89cddb67\
                  191a118531ea0427 97626c9bfd370426 fdf3f59158bf7d4d 43",
#elif CTR_STARTVALUE == 1
   *ctrcipher =
       "6c6bae886c235d8c 7997d45c1bf0bca2 48b4bca9eb396d1b f6945e5b7a4fc10f\
                  488cfe76fd5eaeff 2b8fb469f78fa61e 285e4cf9b9aee3d0 a8",
#endif
   *xtscipher =
       "10f9301a157bfceb 3eb9e7bd38500b7e 959e21ba3cc1179a d7f7d7d99460e695\
                  5e8bcb177571c719 6de58ff28c381913 e7c82d0adfd90c45 ca",
   *ccmcipher =
       "d2575123438338d7 0b2955537fdfcf41 729870884e85af15 f0a74975a72b337d\
                  04d426de87594b9a be3e6dcf07f21c99 db3999f81299d302 ad1e5ba683e9039a\
                  5483685f1bd2c3fa 3b", /*  <---- with 16 bytes tag */
       *gcmcipher =
           "5ceab5b7c2d6dede 555a23c7e3e63274 4075a51df482730b a31485ec987ddcc8\
                  73acdcfc6759a47b a424d838e7c0cb71 b9a4d8f4572e2141 18c8ab284ca845c1\
                  4394618703cddf3a fb", /*  <---- with 16 bytes tag */
           *ocbcipher =
               "fc254896eb785b05 dd87f240722dd935 61f5a0ef6aff2eb6 5953da0b26257ed0\
                  d69cb496e9a0cb1b f646151aa07e629a 28d99f0ffd7ea753 5c39f440df33c988\
                  c55cbcc8ac086ffa 23", /*  <---- with 16 bytes tag */
#if EAXP
               *eaxcipher =
                   "f516e9c20069292c c51ba8b6403ddedf 5a34798f62187f58 d723fa33573fd80b\
                  f08ffbb09dadbd0b 6fa4812ca4bb5e6d db9a384943b36690 e81738a7a1",
#else /*  ↑↑↑↑  with 4 bytes tag  */
               *eaxcipher =
                   "4e2fa1bef9ffc23f 6965ee7135981c91 af9bfe97a6b13c01 b8b99e114dda2391\
                  50661c618335a005 47cca55a8f22fbd5 ed5ab4b4a17d0aa3 29febd14ef271bae\
                  986810a504f01ec6 02", /*  <---- with 16 bytes tag */
#endif
   *gsvcipher =
       "2f1488496ada3f70 9760420ac72e5acf a977f6add4c55ac6 85f1b9dff8f381e0\
                  2a64bbdd64cdd778 525462949bb0b141 db908c5cfa365750 3666f879ac879fcb\
                  f25c15d496a1e6f7 f8", /*  <---- with 16 bytes tag */
       *sivcipher =
           "f6d8137b17d58d13 af040e8abadd965b 9bae3a3de90ca6f7 049c2528767da2cf\
                  ef17de85b1d07b59 d26b0595071ae428 3015840928e2c7f5 9abf06003b14b9ee\
                  25111d34bb2bfcc2 25", /*  16 bytes i.v. PREPENDED */
           *fpe_plain =
               "012345678998765432100123456789987654321001234567899876543",
#if FF_X == 3 /*  <---- MAXLEN=56 if R=10 */
   *fpecipher = "5421127983616113574126130461499164826577083916494538953",
#else
   *fpecipher = "002023830856390748865351321296835380335276971371700355982",
#endif
   *cmac_hash = "b887df1fd8c239c3 e8a64d9822e21128",
   *p1305_mac = "3175bed9bd01821a 62d4c7bef26722be",
   *wrapped = "1FA68B0A8112B447 AEF34BD8FB5A7B82 9D3E862371D2CFE5";
#elif AES_KEY_LENGTH == 24 /*  ↓↓↓↓  PKCS#7 is enabled */
   *ecbcipher =
       "af1893f0fbb09a43 7f6b0fd4f4977890 7bb85cccf1e9d2e3 ebe5bae935107868\
                  c6d72cb2ca375c12 ce6b6b1141141fd0 d268d14db351d680 5aabb99427341da9",
   *wrapped =
       "031D33264E15D332 68F24EC260743EDC E1C6C7DDEE725A93 6BA814915C6762D2";
#else
   *xtscipher =
       "40bfcc14845b1bb4 15dd13abf1e6f89d 3bfd794cf6655ffd 14c0d7e4177eeaf4\
                  5dd95f05663fcfb4 47671154a91b9d00 d1bd7a35c14c7410 9a",
   *wrapped =
       "28C9F404C4B810F4 CBCCB35CFB87F826 3F5786E2D80ED326 CBC7F0E71A99F43B\
                  FB988B9B7A02DD21"; /*  <---- it is in RFC-3394 */
#endif

#include <stdio.h>

static void check(const char *method,
                  void *result,
                  const void *expected,
                  size_t size)
{
    int c = memcmp(expected, result, size);
    printf("AES-%d %s: %s\n", AES_KEY_LENGTH * 8, method,
           c ? "FAILED :(" : "PASSED!");
    memset(result, 0xcc, BuffL);
}

static void str2bytes(const char *str, uint8_t *bytes)
#define char2num(c) (c > '9' ? (c & 7) + 9 : c & 0xF)
{
    unsigned i, j;
    for (i = 0, j = ~0U; str[i]; ++i) {
        if (str[i] < '0' || str[i] > 'f')
            continue;
        if (j++ & 1)
            bytes[j / 2] = char2num(str[i]) << 4;
        else
            bytes[j / 2] |= char2num(str[i]);
    }
}

int main()
{
    uint8_t mainKey[32], key[64], iv[16], input[BuffL - 16], test[BuffL],
        output[BuffL], *a = mainKey + 1, sa = sizeof mainKey - 1,
                       st = TestStringSize / 2;
    str2bytes(cipherKey, key);
    str2bytes(secretKey, key + 32);
    str2bytes(masterKey, mainKey);
    str2bytes(iVec, iv);
    str2bytes(plainText, input);
    printf("%s %s Test results\n", __DATE__, __TIME__);

#if M_RIJNDAEL
    memcpy(input + 48, iv, 16);
    a = AES_KEY_LENGTH == 16 ? key : input + (AES_KEY_LENGTH - 24) * 4;

    memcpy(test, a, 32);
    memcpy(test + 32, mainKey, 16);
    memcpy(test + 48, key + 32, 16);
    AES_Cipher(key + 32, 'E', mainKey, output);
    AES_Cipher(mainKey, 'E', key + 32, output + 16);
    AES_Cipher(key + 32, 'D', a, output + 32);
    AES_Cipher(mainKey, 'D', a + 16, output + 48);
    check("encryption & decryption", output, test, 64);
#endif
#if ECB && AES_KEY_LENGTH + 8 * !AES_PADDING == 24
    str2bytes(ecbcipher, test);
    AES_ECB_encrypt(key, input, st, output);
    check("ECB encryption", output, test, sizeof input);
    AES_ECB_decrypt(key, test, sizeof input, output);
    check("ECB decryption", output, input, st);
#endif
#if CBC && AES_KEY_LENGTH == 16
    str2bytes(cbccipher, test);
    AES_CBC_encrypt(key, iv, input, st, output);
    check("CBC encryption", output, test, CTS ? st : sizeof input);
    AES_CBC_decrypt(key, iv, test, CTS ? st : sizeof input, output);
    check("CBC decryption", output, input, st);
#endif
#if CFB && AES_KEY_LENGTH == 16
    str2bytes(cfbcipher, test);
    AES_CFB_encrypt(key, iv, input, st, output);
    check("CFB encryption", output, test, st);
    AES_CFB_decrypt(key, iv, test, st, output);
    check("CFB decryption", output, input, st);
#endif
#if OFB && AES_KEY_LENGTH == 16
    str2bytes(ofbcipher, test);
    AES_OFB_encrypt(key, iv, input, st, output);
    check("OFB encryption", output, test, st);
    AES_OFB_decrypt(key, iv, test, st, output);
    check("OFB decryption", output, input, st);
#endif
#if CTR_NA && AES_KEY_LENGTH == 16
    str2bytes(ctrcipher, test);
    AES_CTR_encrypt(key, iv, input, st, output);
    check("CTR encryption", output, test, st);
    AES_CTR_decrypt(key, iv, test, st, output);
    check("CTR decryption", output, input, st);
#endif
#if XTS && AES_KEY_LENGTH != 24
    str2bytes(xtscipher, test);
    AES_XTS_encrypt(key, iv, input, st, output);
    check("XTS encryption", output, test, st);
    AES_XTS_decrypt(key, iv, test, st, output);
    check("XTS decryption", output, input, st);
#endif
#if CMAC && AES_KEY_LENGTH == 16
    str2bytes(cmac_hash, test);
    AES_CMAC(key, input, st, output);
    check("validate CMAC ", output, test, 16);
#endif
#if POLY1305 && AES_KEY_LENGTH == 16
    str2bytes(p1305_mac, test);
    AES_Poly1305(key, iv, input, st, output);
    check("Poly-1305 mac ", output, test, 16);
#endif
#if GCM && AES_KEY_LENGTH == 16
    str2bytes(gcmcipher, test);
    AES_GCM_encrypt(key, iv, input, st, a, sa, output, output + st);
    check("GCM encryption", output, test, st + 16);
    *output ^= AES_GCM_decrypt(key, iv, test, st, a, sa, 16, output);
    check("GCM decryption", output, input, st);
#endif
#if CCM && AES_KEY_LENGTH == 16
    str2bytes(ccmcipher, test);
    AES_CCM_encrypt(key, iv, input, st, a, sa, output, output + st);
    check("CCM encryption", output, test, st + CCM_TAG_LEN);
    *output ^= AES_CCM_decrypt(key, iv, test, st, a, sa, CCM_TAG_LEN, output);
    check("CCM decryption", output, input, st);
#endif
#if OCB && AES_KEY_LENGTH == 16
    str2bytes(ocbcipher, test);
    AES_OCB_encrypt(key, iv, input, st, a, sa, output, output + st);
    check("OCB encryption", output, test, st + OCB_TAG_LEN);
    *output ^= AES_OCB_decrypt(key, iv, test, st, a, sa, OCB_TAG_LEN, output);
    check("OCB decryption", output, input, st);
#endif
#if SIV && AES_KEY_LENGTH == 16
    str2bytes(sivcipher, test);
    AES_SIV_encrypt(key, input, st, a, sa, output, output + 16);
    check("SIV encryption", output, test, st + 16);
    *output ^= AES_SIV_decrypt(key, test, test + 16, st, a, sa, output);
    check("SIV decryption", output, input, st);
#endif
#if GCM_SIV && AES_KEY_LENGTH == 16
    str2bytes(gsvcipher, test);
    GCM_SIV_encrypt(key, iv, input, st, a, sa, output, output + st);
    check("GCMSIV encrypt", output, test, st + 16);
    *output ^= GCM_SIV_decrypt(key, iv, test, st, a, sa, 16, output);
    check("GCMSIV decrypt", output, input, st);
#endif
#if EAX && AES_KEY_LENGTH == 16
    str2bytes(eaxcipher, test);
#if EAXP
    AES_EAX_encrypt(key, a, input, st, sa, output);
    check("EAX encryption", output, test, st + 4);
    *output ^= AES_EAX_decrypt(key, a, test, st, sa, output);
#else
    AES_EAX_encrypt(key, iv, input, st, a, sa, output, output + st);
    check("EAX encryption", output, test, st + 16);
    *output ^= AES_EAX_decrypt(key, iv, test, st, a, sa, 16, output);
#endif
    check("EAX decryption", output, input, st);
#endif
#if KWA
    str2bytes(wrapped, test);
    AES_KEY_wrap(mainKey, key + 32, AES_KEY_LENGTH, output);
    check("key wrapping  ", output, test, AES_KEY_LENGTH + 8);
    AES_KEY_unwrap(mainKey, test, AES_KEY_LENGTH + 8, output);
    check("key unwrapping", output, key + 32, AES_KEY_LENGTH);
#endif
#if FPE && AES_KEY_LENGTH == 16 && CUSTOM_ALPHABET == 0
    memcpy(test, fpecipher, FF_X == 3 ? (st = 55) : st);
#if FF_X == 3
    AES_FPE_encrypt(key, a, fpe_plain, st, output);
    check("FF3 encryption", output, test, st);
    AES_FPE_decrypt(key, a, test, st, output);
#else
    AES_FPE_encrypt(key, a, sa, fpe_plain, st, output);
    check("FF1 encryption", output, test, st);
    AES_FPE_decrypt(key, a, sa, test, st, output);
#endif
    check("FPE decryption", output, fpe_plain, st);
#endif

    /** a template for "OFFICIAL TEST VECTORS":  */
#if OCB && EAX && SIV && GCM_SIV && POLY1305 && FPE && AES_KEY_LENGTH == 16
    printf("+-> Let's do some extra tests\n");

    st = sa = 24; /* taken from RFC 7253:  */
    str2bytes("000102030405060708090A0B0C0D0E0F", key);
    str2bytes("BBAA99887766554433221107", iv);
    str2bytes("000102030405060708090A0B0C0D0E0F1011121314151617", a);
    str2bytes("000102030405060708090A0B0C0D0E0F1011121314151617", input);
    str2bytes(
        "1CA2207308C87C010756104D8840CE1952F09673A448A122\
               C92C62241051F57356D7F3C90BB0E07F",
        test);
    AES_OCB_encrypt(key, iv, input, st, a, sa, output, output + st);
    check("OCB encryption", output, test, st + OCB_TAG_LEN);
    *output ^= AES_OCB_decrypt(key, iv, test, st, a, sa, OCB_TAG_LEN, output);
    check("OCB decryption", output, input, st);

    st = 11;
    sa = 7; /* taken from RFC 8452:  */
    str2bytes("ee8e1ed9ff2540ae8f2ba9f50bc2f27c", key);
    str2bytes("752abad3e0afb5f434dc4310", iv);
    str2bytes("6578616d706c65", a);
    str2bytes("48656c6c6f20776f726c64", input);
    str2bytes("5d349ead175ef6b1def6fd4fbcdeb7e4793f4a1d7e4faa70100af1", test);
    GCM_SIV_encrypt(key, iv, input, st, a, sa, output, output + st);
    check("GCMSIV encrypt", output, test, st + 16);
    *output ^= GCM_SIV_decrypt(key, iv, test, st, a, sa, 16, output);
    check("GCMSIV decrypt", output, input, st);
    st = 12;
    sa = 1; /* taken from RFC 8452:  */
    str2bytes("01000000000000000000000000000000", key);
    str2bytes("030000000000000000000000", iv);
    str2bytes("01", a);
    str2bytes("020000000000000000000000", input);
    str2bytes(
        "296c7889fd99f41917f4462008299c51\
               02745aaa3a0c469fad9e075a",
        test);
    GCM_SIV_encrypt(key, iv, input, st, a, sa, output, output + st);
    check("GCMSIV encrypt", output, test, st + 16);
    *output ^= GCM_SIV_decrypt(key, iv, test, st, a, sa, 16, output);
    check("GCMSIV decrypt", output, input, st);

    st = 14;
    sa = 24; /* taken from RFC 5297:  */
    str2bytes(
        "fffefdfc fbfaf9f8 f7f6f5f4 f3f2f1f0\
               f0f1f2f3 f4f5f6f7 f8f9fafb fcfdfeff",
        key);
    str2bytes(
        "10111213 14151617 18191a1b 1c1d1e1f\
               20212223 24252627",
        a);
    str2bytes("11223344 55667788 99aabbcc ddee", input);
    str2bytes(
        "85632d07 c6e8f37f 950acd32 0a2ecc93\
               40c02b96 90c4dc04 daef7f6a fe5c",
        test);
    AES_SIV_encrypt(key, input, st, a, sa, output, output + 16);
    check("SIV encryption", output, test, st + 16);
    *output ^= AES_SIV_decrypt(key, test, test + 16, st, a, sa, output);
    check("SIV decryption", output, input, st);
    st = 16;
    sa = 0; /* from miscreant: https://bit.ly/3yc2GBs  */
    str2bytes(
        "fffefdfcfbfaf9f8f7f6f5f4f3f2f1f0f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff",
        key);
    str2bytes("00112233445566778899aabbccddeeff", input);
    str2bytes(
        "f304f912863e303d5b540e5057c7010c942ffaf45b0e5ca5fb9a56a5263bb065",
        test);
    AES_SIV_encrypt(key, input, st, a, sa, output, output + 16);
    check("SIV encryption", output, test, st + 16);
    *output ^= AES_SIV_decrypt(key, test, test + 16, st, a, sa, output);
    check("SIV decryption", output, input, st);
#if EAXP
    st = 0;
    sa = 50; /* from Annex G of the IEEE Std 1703-2012  */
    str2bytes("01020304050607080102030405060708", mainKey);
    str2bytes(
        "A20D060B607C86F7540116007BC175A8\
               03020100BE0D280B810984A60C060A60\
               7C86F7540116007B040248F3C2040330\
               0005",
        test);
    str2bytes("515AE775", key);
    AES_EAX_encrypt(mainKey, test, input, st, sa, output);
    check("EAX encryption", output, key, st + 4);
    *output ^= AES_EAX_decrypt(mainKey, test, key, st, sa, output);
    check("EAX decryption", output, input, st);
    st = 28;
    sa = 65; /* from Moise-Beroset-Phinney-Burns paper: */
    str2bytes("10 20 30 40 50 60 70 80 90 a0 b0 c0 d0 e0 f0 00", mainKey);
    str2bytes(
        "a2 0e 06 0c 60 86 48 01 86 fc 2f 81 1c aa 4e 01\
               a8 06 02 04 39 a0 0e bb ac 0f a2 0d a0 0b a1 09\
               80 01 00 81 04 4b ce e2 c3 be 25 28 23 81 21 88\
               a6 0a 06 08 2b 06 01 04 01 82 85 63 00 4b ce e2\
               c3",
        test);
    str2bytes(
        "17 51 30 30 30 30 30 30 30 30 30 30 30 30 30 30\
               30 30 30 30 30 30 00 00 03 30 00 01",
        input);
    str2bytes(
        "9c f3 2c 7e c2 4c 25 0b e7 b0 74 9f ee e7 1a 22\
               0d 0e ee 97 6e c2 3d bf 0c aa 08 ea 00 54 3e 66",
        key);
    AES_EAX_encrypt(mainKey, test, input, st, sa, output);
    check("EAX encryption", output, key, st + 4);
    *output ^= AES_EAX_decrypt(mainKey, test, key, st, sa, output);
#else
    st = 12;
    sa = 8; /* from Bellare-Rogaway-Wagner 2004 paper: */
    str2bytes("BD8E6E11475E60B268784C38C62FEB22", key);
    str2bytes("6EAC5C93072D8E8513F750935E46DA1B", iv);
    str2bytes("D4482D1CA78DCE0F", a);
    str2bytes("4DE3B35C3FC039245BD1FB7D", input);
    str2bytes("835BB4F15D743E350E728414ABB8644FD6CCB86947C5E10590210A4F", test);
    AES_EAX_encrypt(key, iv, input, st, a, sa, output, output + st);
    check("EAX encryption", output, test, st + 16);
    *output ^= AES_EAX_decrypt(key, iv, test, st, a, sa, 16, output);
#endif
    check("EAX decryption", output, input, st);

    st = 32; /* D.J.B. [2005] https://cr.yp.to/mac.html */
    str2bytes(
        "66 3c ea 19 0f fb 83 d8 95 93 f3 f4 76 b6 bc 24\
               d7 e6 79 10 7e a2 6a db 8c af 66 52 d0 65 61 36",
        input);
    str2bytes(
        "6a cb 5f 61 a7 17 6d d3 20 c5 c1 eb 2e dc dc 74\
               48 44 3d 0b b0 d2 11 09 c8 9a 10 0b 5c e2 c2 08",
        key);
    str2bytes("ae 21 2a 55 39 97 29 59 5d ea 45 8b c6 21 ff 0e", iv);
    str2bytes("0e e1 c1 6b b7 3f 0f 4f d1 98 81 75 3c 01 cd be", test);
    AES_Poly1305(key, iv, input, st, output);
    check("Poly-1305 mac ", output, test, 16);
    st = 63;
    str2bytes(
        "ab 08 12 72 4a 7f 1e 34 27 42 cb ed 37 4d 94 d1\
               36 c6 b8 79 5d 45 b3 81 98 30 f2 c0 44 91 fa f0\
               99 0c 62 e4 8b 80 18 b2 c3 e4 a0 fa 31 34 cb 67\
               fa 83 e1 58 c9 94 d9 61 c4 cb 21 09 5c 1b f9",
        input);
    str2bytes(
        "e1 a5 66 8a 4d 5b 66 a5 f6 8c c5 42 4e d5 98 2d\
               12 97 6a 08 c4 42 6d 0c e8 a8 24 07 c4 f4 82 07",
        key);
    str2bytes("9a e8 31 e7 43 97 8d 3a 23 52 7c 71 28 14 9e 3a", iv);
    str2bytes("51 54 ad 0d 2c b2 6e 01 27 4f c5 11 48 49 1f 1b", test);
    AES_Poly1305(key, iv, input, st, output);
    check("Poly-1305 mac ", output, test, 16);
#if FF_X == 3 && CUSTOM_ALPHABET == 0
    st = 29; /* see the comment in function: FF3_Cipher */
    str2bytes("EF 43 59 D8 D5 80 AA 4F 7F 03 6D 6F 04 FC 6A 94", key);
    str2bytes("00 00 00 00 00 00 00 00", a);
    memcpy(input, "89012123456789000000789000000", st);
    memcpy(output, "34695224821734535122613701434", st);
    AES_FPE_encrypt(key, a, input, st, test);
    check("FF3 encryption", test, output, st);
    AES_FPE_decrypt(key, a, output, st, test);
    check("FF3 decryption", test, input, st);
#elif FF_X != 3 && CUSTOM_ALPHABET == 3
    st = 19;
    sa = 11;
    str2bytes("2B 7E 15 16 28 AE D2 A6 AB F7 15 88 09 CF 4F 3C", key);
    str2bytes("37 37 37 37 70 71 72 73 37 37 37", a);
    memcpy(input, "0123456789abcdefghi", st);
    memcpy(output, "a9tv40mll9kdu509eum", st);
    AES_FPE_encrypt(key, a, sa, input, st, test);
    check("FF1 encryption", test, output, st);
    AES_FPE_decrypt(key, a, sa, output, st, test);
    check("FF1 decryption", test, input, st);
#endif
#endif
    return 0;
}
