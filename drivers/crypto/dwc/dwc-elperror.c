#include "dwc-elperror.h"

/* Convert SDK error codes to corresponding kernel error codes. */
int dwc_elp_errorcode(int code) {
   switch (code) {
   case CRYPTO_INPROGRESS:
       return -EINPROGRESS;
   case CRYPTO_INVALID_HANDLE:
   case CRYPTO_INVALID_CONTEXT:
       return -ENXIO;
   case CRYPTO_NOT_INITIALIZED:
       return -ENODATA;
   case CRYPTO_INVALID_SIZE:
   case CRYPTO_INVALID_ALG:
   case CRYPTO_INVALID_KEY_SIZE:
   case CRYPTO_INVALID_ARGUMENT:
   case CRYPTO_INVALID_BLOCK_ALIGNMENT:
   case CRYPTO_INVALID_MODE:
   case CRYPTO_INVALID_KEY:
   case CRYPTO_INVALID_IV_SIZE:
   case CRYPTO_INVALID_ICV_KEY_SIZE:
   case CRYPTO_INVALID_PARAMETER_SIZE:
   case CRYPTO_REPLAY:
   case CRYPTO_INVALID_PROTOCOL:
       return -EINVAL;
   case CRYPTO_NOT_IMPLEMENTED:
   case CRYPTO_MODULE_DISABLED:
       return -ENOTSUPP;
   case CRYPTO_NO_MEM:
       return -ENOMEM;
   case CRYPTO_INVALID_PAD:
   case CRYPTO_INVALID_SEQUENCE:
       return -EILSEQ;
   case CRYPTO_MEMORY_ERROR:
       return -ETIMEDOUT;
   case CRYPTO_HALTED:
       return -ECANCELED;
   case CRYPTO_AUTHENTICATION_FAILED:
   case CRYPTO_SEQUENCE_OVERFLOW:
   case CRYPTO_INVALID_VERSION:
       return -EPROTO;
   case CRYPTO_FIFO_FULL:
       return -EBUSY;
   case CRYPTO_SRM_FAILED:
   case CRYPTO_DISABLED:
   case CRYPTO_LAST_ERROR:
       return -EAGAIN;
   case CRYPTO_FAILED:
   case CRYPTO_FATAL:
       return -EIO;
   case CRYPTO_INVALID_FIRMWARE:
       return -ENOENT;
   } 
   return code;
}
