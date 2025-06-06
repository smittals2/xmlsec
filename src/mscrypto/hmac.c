/*
 * XML Security Library (http://www.aleksey.com/xmlsec).
 *
 * HMAC transforms implementation for Microsoft Crypto API.
 *
 * This is free software; see Copyright file in the source
 * distribution for preciese wording.
 *
 * Copyright (C) 2002-2024 Aleksey Sanin <aleksey@aleksey.com>. All Rights Reserved.
 */
/**
 * SECTION:crypto
 */


#ifndef XMLSEC_NO_HMAC
#include "globals.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <xmlsec/xmlsec.h>
#include <xmlsec/base64.h>
#include <xmlsec/errors.h>
#include <xmlsec/keys.h>
#include <xmlsec/transforms.h>
#include <xmlsec/private.h>

#include <xmlsec/mscrypto/crypto.h>

#include "private.h"
#include "../cast_helpers.h"
#include "../keysdata_helpers.h"
#include "../transform_helpers.h"


/**************************************************************************
 *
 * Configuration
 *
 *****************************************************************************/

/******************************************************************************
 *
 * Internal MSCrypto HMAC CTX
 *
 * [HMAC Algorithm support](http://www.w3.org/TR/xmldsig-core/#sec-HMAC):
 * The HMAC algorithm (RFC2104 [HMAC]) takes the truncation length in bits
 * as a parameter; if the parameter is not specified then all the bits of the
 * hash are output. An example of an HMAC SignatureMethod element:
 *
 * |[<!-- language="XML" -->
 * <SignatureMethod Algorithm="http://www.w3.org/2000/09/xmldsig#hmac-sha1">
 *   <HMACOutputLength>128</HMACOutputLength>
 * </SignatureMethod>
 * |]

 *****************************************************************************/
typedef struct _xmlSecMSCryptoHmacCtx            xmlSecMSCryptoHmacCtx, *xmlSecMSCryptoHmacCtxPtr;
struct _xmlSecMSCryptoHmacCtx {
    HCRYPTPROV      provider;
    HCRYPTKEY       cryptKey;
    HCRYPTKEY       pubPrivKey;
    ALG_ID          alg_id;
    const xmlSecMSCryptoProviderInfo  * providers;
    HCRYPTHASH      mscHash;
    unsigned char   dgst[XMLSEC_TRANSFORM_HMAC_MAX_OUTPUT_SIZE];
    xmlSecSize      dgstSize;
    xmlSecSize      dgstSizeInBits;   /* dgst size in bytes */
    int             ctxInitialized;
};

/******************************************************************************
 *
 * HMAC transforms
 *
 *****************************************************************************/
XMLSEC_TRANSFORM_DECLARE(MSCryptoHmac, xmlSecMSCryptoHmacCtx)
#define xmlSecMSCryptoHmacSize XMLSEC_TRANSFORM_SIZE(MSCryptoHmac)

static int      xmlSecMSCryptoHmacCheckId                        (xmlSecTransformPtr transform);
static int      xmlSecMSCryptoHmacInitialize                     (xmlSecTransformPtr transform);
static void     xmlSecMSCryptoHmacFinalize                       (xmlSecTransformPtr transform);
static int      xmlSecMSCryptoHmacNodeRead                       (xmlSecTransformPtr transform,
                                                                 xmlNodePtr node,
                                                                 xmlSecTransformCtxPtr transformCtx);
static int      xmlSecMSCryptoHmacSetKeyReq                      (xmlSecTransformPtr transform,
                                                                 xmlSecKeyReqPtr keyReq);
static int      xmlSecMSCryptoHmacSetKey                         (xmlSecTransformPtr transform,
                                                                 xmlSecKeyPtr key);
static int      xmlSecMSCryptoHmacVerify                         (xmlSecTransformPtr transform,
                                                                 const xmlSecByte* data,
                                                                 xmlSecSize dataSize,
                                                                 xmlSecTransformCtxPtr transformCtx);
static int      xmlSecMSCryptoHmacExecute                        (xmlSecTransformPtr transform,
                                                                 int last,
                                                                 xmlSecTransformCtxPtr transformCtx);

/* Ordered list of providers to search for algorithm implementation using
 * xmlSecMSCryptoFindProvider() function
 *
 * MUST END with { NULL, 0 } !!!
 */
static xmlSecMSCryptoProviderInfo xmlSecMSCryptoProviderInfo_Hmac[] = {
    { XMLSEC_CRYPTO_MS_ENH_RSA_AES_PROV,                PROV_RSA_AES},
    { XMLSEC_CRYPTO_MS_ENH_RSA_AES_PROV_PROTOTYPE,      PROV_RSA_AES },
    { MS_STRONG_PROV,                                   PROV_RSA_FULL },
    { MS_ENHANCED_PROV,                                 PROV_RSA_FULL },
    { MS_DEF_PROV,                                      PROV_RSA_FULL },
    { NULL, 0 }
};

static int
xmlSecMSCryptoHmacCheckId(xmlSecTransformPtr transform) {

#ifndef XMLSEC_NO_SHA1
    if(xmlSecTransformCheckId(transform, xmlSecMSCryptoTransformHmacSha1Id)) {
        return(1);
    } else
#endif /* XMLSEC_NO_SHA1 */

#ifndef XMLSEC_NO_SHA256
    if(xmlSecTransformCheckId(transform, xmlSecMSCryptoTransformHmacSha256Id)) {
        return(1);
    } else
#endif /* XMLSEC_NO_SHA256 */

#ifndef XMLSEC_NO_SHA384
    if(xmlSecTransformCheckId(transform, xmlSecMSCryptoTransformHmacSha384Id)) {
        return(1);
    } else
#endif /* XMLSEC_NO_SHA384 */

#ifndef XMLSEC_NO_SHA512
    if(xmlSecTransformCheckId(transform, xmlSecMSCryptoTransformHmacSha512Id)) {
        return(1);
    } else
#endif /* XMLSEC_NO_SHA512 */

#ifndef XMLSEC_NO_MD5
    if(xmlSecTransformCheckId(transform, xmlSecMSCryptoTransformHmacMd5Id)) {
        return(1);
    } else
#endif /* XMLSEC_NO_MD5 */

    /* not found */
    {
        return(0);
    }
}

static int
xmlSecMSCryptoHmacInitialize(xmlSecTransformPtr transform) {
    xmlSecMSCryptoHmacCtxPtr ctx;

    xmlSecAssert2(xmlSecMSCryptoHmacCheckId(transform), -1);
    xmlSecAssert2(xmlSecTransformCheckSize(transform, xmlSecMSCryptoHmacSize), -1);

    ctx = xmlSecMSCryptoHmacGetCtx(transform);
    xmlSecAssert2(ctx != NULL, -1);

    /* initialize context */
    memset(ctx, 0, sizeof(xmlSecMSCryptoHmacCtx));

#ifndef XMLSEC_NO_SHA1
    if(xmlSecTransformCheckId(transform, xmlSecMSCryptoTransformHmacSha1Id)) {
        ctx->alg_id = CALG_SHA1;
        ctx->providers = xmlSecMSCryptoProviderInfo_Hmac;
    } else
#endif /* XMLSEC_NO_SHA1 */

#ifndef XMLSEC_NO_SHA256
    if(xmlSecTransformCheckId(transform, xmlSecMSCryptoTransformHmacSha256Id)) {
        ctx->alg_id = CALG_SHA_256;
        ctx->providers = xmlSecMSCryptoProviderInfo_Hmac;
    } else
#endif /* XMLSEC_NO_SHA256 */

#ifndef XMLSEC_NO_SHA384
    if(xmlSecTransformCheckId(transform, xmlSecMSCryptoTransformHmacSha384Id)) {
        ctx->alg_id = CALG_SHA_384;
        ctx->providers = xmlSecMSCryptoProviderInfo_Hmac;
    } else
#endif /* XMLSEC_NO_SHA384 */

#ifndef XMLSEC_NO_SHA512
    if(xmlSecTransformCheckId(transform, xmlSecMSCryptoTransformHmacSha512Id)) {
        ctx->alg_id = CALG_SHA_512;
        ctx->providers = xmlSecMSCryptoProviderInfo_Hmac;
    } else
#endif /* XMLSEC_NO_SHA512 */

#ifndef XMLSEC_NO_MD5
    if(xmlSecTransformCheckId(transform, xmlSecMSCryptoTransformHmacMd5Id)) {
        ctx->alg_id = CALG_MD5;
        ctx->providers = xmlSecMSCryptoProviderInfo_Hmac;
    } else
#endif /* XMLSEC_NO_MD5 */

    /* not found */
    {
        xmlSecInvalidTransfromError(transform)
        return(-1);
    }

    ctx->provider = xmlSecMSCryptoFindProvider(ctx->providers, NULL, CRYPT_VERIFYCONTEXT, TRUE);
    if(ctx->provider == 0) {
        xmlSecInternalError("xmlSecMSCryptoFindProvider",
                            xmlSecTransformGetName(transform));
        return(-1);
    }

    /* Create dummy key to be able to import plain session keys */
    if (!xmlSecMSCryptoCreatePrivateExponentOneKey(ctx->provider, &(ctx->pubPrivKey))) {
        xmlSecMSCryptoError("xmlSecMSCryptoCreatePrivateExponentOneKey",
                            xmlSecTransformGetName(transform));

        return(-1);
    }

    return(0);
}

static void
xmlSecMSCryptoHmacFinalize(xmlSecTransformPtr transform) {
    xmlSecMSCryptoHmacCtxPtr ctx;

    xmlSecAssert(xmlSecMSCryptoHmacCheckId(transform));
    xmlSecAssert(xmlSecTransformCheckSize(transform, xmlSecMSCryptoHmacSize));

    ctx = xmlSecMSCryptoHmacGetCtx(transform);
    xmlSecAssert(ctx != NULL);

    if(ctx->mscHash != 0) {
        CryptDestroyHash(ctx->mscHash);
    }
    if (ctx->cryptKey) {
        CryptDestroyKey(ctx->cryptKey);
    }
    if (ctx->pubPrivKey) {
        CryptDestroyKey(ctx->pubPrivKey);
    }
    if(ctx->provider != 0) {
        CryptReleaseContext(ctx->provider, 0);
    }

    memset(ctx, 0, sizeof(xmlSecMSCryptoHmacCtx));
}

static int
xmlSecMSCryptoHmacNodeRead(xmlSecTransformPtr transform, xmlNodePtr node,
                           xmlSecTransformCtxPtr transformCtx XMLSEC_ATTRIBUTE_UNUSED) {
    xmlSecMSCryptoHmacCtxPtr ctx;
    int ret;

    xmlSecAssert2(xmlSecMSCryptoHmacCheckId(transform), -1);
    xmlSecAssert2(xmlSecTransformCheckSize(transform, xmlSecMSCryptoHmacSize), -1);
    xmlSecAssert2(node!= NULL, -1);
    UNREFERENCED_PARAMETER(transformCtx);

    ctx = xmlSecMSCryptoHmacGetCtx(transform);
    xmlSecAssert2(ctx != NULL, -1);

    ret = xmlSecTransformHmacReadOutputBitsSize(node, ctx->dgstSizeInBits, &ctx->dgstSizeInBits);
    if (ret < 0) {
        xmlSecInternalError("xmlSecTransformHmacReadOutputBitsSize()",
            xmlSecTransformGetName(transform));
        return(-1);
    }

    return(0);
}

static int
xmlSecMSCryptoHmacSetKeyReq(xmlSecTransformPtr transform,  xmlSecKeyReqPtr keyReq) {
    xmlSecAssert2(xmlSecMSCryptoHmacCheckId(transform), -1);
    xmlSecAssert2((transform->operation == xmlSecTransformOperationSign) || (transform->operation == xmlSecTransformOperationVerify), -1);
    xmlSecAssert2(xmlSecTransformCheckSize(transform, xmlSecMSCryptoHmacSize), -1);
    xmlSecAssert2(keyReq != NULL, -1);

    keyReq->keyId   = xmlSecMSCryptoKeyDataHmacId;
    keyReq->keyType = xmlSecKeyDataTypeSymmetric;
    if(transform->operation == xmlSecTransformOperationSign) {
        keyReq->keyUsage = xmlSecKeyUsageSign;
    } else {
        keyReq->keyUsage = xmlSecKeyUsageVerify;
    }

    return(0);
}

static int
xmlSecMSCryptoHmacSetKey(xmlSecTransformPtr transform, xmlSecKeyPtr key) {
    xmlSecMSCryptoHmacCtxPtr ctx;
    xmlSecKeyDataPtr value;
    xmlSecBufferPtr buffer;
    HMAC_INFO hmacInfo;
    xmlSecByte* bufPtr;
    xmlSecSize bufSize;
    DWORD dwBufSize;
    int ret;

    xmlSecAssert2(xmlSecMSCryptoHmacCheckId(transform), -1);
    xmlSecAssert2((transform->operation == xmlSecTransformOperationSign) || (transform->operation == xmlSecTransformOperationVerify), -1);
    xmlSecAssert2(xmlSecTransformCheckSize(transform, xmlSecMSCryptoHmacSize), -1);
    xmlSecAssert2(key != NULL, -1);

    ctx = xmlSecMSCryptoHmacGetCtx(transform);
    xmlSecAssert2(ctx != NULL, -1);
    xmlSecAssert2(ctx->ctxInitialized == 0, -1);
    xmlSecAssert2(ctx->provider != 0, -1);
    xmlSecAssert2(ctx->pubPrivKey != 0, -1);
    xmlSecAssert2(ctx->cryptKey == 0, -1);
    xmlSecAssert2(ctx->mscHash == 0, -1);

    value = xmlSecKeyGetValue(key);
    xmlSecAssert2(xmlSecKeyDataCheckId(value, xmlSecMSCryptoKeyDataHmacId), -1);

    buffer = xmlSecKeyDataBinaryValueGetBuffer(value);
    xmlSecAssert2(buffer != NULL, -1);

    bufPtr = xmlSecBufferGetData(buffer);
    bufSize = xmlSecBufferGetSize(buffer);
    if((bufPtr == NULL) || (bufSize == 0)) {
        xmlSecInvalidZeroKeyDataSizeError(xmlSecTransformGetName(transform));
        return(-1);
    }
    XMLSEC_SAFE_CAST_SIZE_TO_ULONG(bufSize, dwBufSize, return(-1), xmlSecTransformGetName(transform));

    /* Import this key and get an HCRYPTKEY handle.
     *
     * HACK!!! HACK!!! HACK!!!
     *
     * Using CALG_RC2 instead of CALG_HMAC for the key algorithm so we don't want to check key length
     */
    if (!xmlSecMSCryptoImportPlainSessionBlob(ctx->provider,
        ctx->pubPrivKey,
        CALG_RC2,
        bufPtr,
        dwBufSize,
        FALSE,
        &(ctx->cryptKey)
        ) || (ctx->cryptKey == 0))  {

        xmlSecInternalError("xmlSecMSCryptoImportPlainSessionBlob",
                            xmlSecTransformGetName(transform));
        return(-1);
    }

    /* create hash */
    ret = CryptCreateHash(ctx->provider,
        CALG_HMAC,
        ctx->cryptKey,
        0,
        &(ctx->mscHash));
    if((ret == 0) || (ctx->mscHash == 0)) {
        xmlSecMSCryptoError("CryptCreateHash",
                            xmlSecTransformGetName(transform));
        return(-1);
    }

    /* set parameters */
    memset(&hmacInfo, 0, sizeof(hmacInfo));
    hmacInfo.HashAlgid = ctx->alg_id;
    ret = CryptSetHashParam(ctx->mscHash, HP_HMAC_INFO, (BYTE*)&hmacInfo, 0);
    if(ret == 0) {
        xmlSecMSCryptoError("CryptSetHashParam",
                            xmlSecTransformGetName(transform));
        return(-1);
    }

    /* done */
    ctx->ctxInitialized = 1;
    return(0);
}

static int
xmlSecMSCryptoHmacVerify(xmlSecTransformPtr transform,
                        const xmlSecByte* data, xmlSecSize dataSize,
                        xmlSecTransformCtxPtr transformCtx XMLSEC_ATTRIBUTE_UNUSED) {
    xmlSecMSCryptoHmacCtxPtr ctx;
    int ret;

    xmlSecAssert2(xmlSecTransformIsValid(transform), -1);
    xmlSecAssert2(xmlSecTransformCheckSize(transform, xmlSecMSCryptoHmacSize), -1);
    xmlSecAssert2(transform->operation == xmlSecTransformOperationVerify, -1);
    xmlSecAssert2(transform->status == xmlSecTransformStatusFinished, -1);
    xmlSecAssert2(data != NULL, -1);
    UNREFERENCED_PARAMETER(transformCtx);

    ctx = xmlSecMSCryptoHmacGetCtx(transform);
    xmlSecAssert2(ctx != NULL, -1);
    xmlSecAssert2(ctx->dgstSizeInBits > 0, -1);

    /* Returns 1 for match, 0 for no match, <0 for errors. */
    ret = xmlSecTransformHmacVerify(data, dataSize, ctx->dgst, ctx->dgstSizeInBits, ctx->dgstSize);
    if (ret < 0) {
        xmlSecInternalError("xmlSecTransformHmacVerify", xmlSecTransformGetName(transform));
        return(-1);
    }
    if (ret == 1) {
        transform->status = xmlSecTransformStatusOk;
    }
    else {
        transform->status = xmlSecTransformStatusFail;
    }
    return(0);
}

static int
xmlSecMSCryptoHmacExecute(xmlSecTransformPtr transform, int last, xmlSecTransformCtxPtr transformCtx) {
    xmlSecMSCryptoHmacCtxPtr ctx;
    xmlSecBufferPtr in, out;
    int ret;

    xmlSecAssert2(xmlSecTransformIsValid(transform), -1);
    xmlSecAssert2((transform->operation == xmlSecTransformOperationSign) || (transform->operation == xmlSecTransformOperationVerify), -1);
    xmlSecAssert2(xmlSecTransformCheckSize(transform, xmlSecMSCryptoHmacSize), -1);
    xmlSecAssert2(transformCtx != NULL, -1);

    in = &(transform->inBuf);
    out = &(transform->outBuf);

    ctx = xmlSecMSCryptoHmacGetCtx(transform);
    xmlSecAssert2(ctx != NULL, -1);
    xmlSecAssert2(ctx->ctxInitialized != 0, -1);

    if(transform->status == xmlSecTransformStatusNone) {
        /* we should be already initialized when we set key */
        transform->status = xmlSecTransformStatusWorking;
    }

    if(transform->status == xmlSecTransformStatusWorking) {
        xmlSecSize inSize;

        inSize = xmlSecBufferGetSize(in);
        if(inSize > 0) {
            DWORD dwInSize;

            XMLSEC_SAFE_CAST_SIZE_TO_ULONG(inSize, dwInSize, return(-1), xmlSecTransformGetName(transform));
            ret = CryptHashData(ctx->mscHash,
                xmlSecBufferGetData(in),
                dwInSize,
                0);

            if(ret == 0) {
                xmlSecMSCryptoError2("CryptHashData", xmlSecTransformGetName(transform),
                    "size=" XMLSEC_SIZE_FMT, inSize);
                return(-1);
            }

            ret = xmlSecBufferRemoveHead(in, inSize);
            if(ret < 0) {
                xmlSecInternalError2("xmlSecBufferRemoveHead", xmlSecTransformGetName(transform),
                    "size=" XMLSEC_SIZE_FMT, inSize);
                return(-1);
            }
        }

        if(last) {
            DWORD retLen = XMLSEC_TRANSFORM_HMAC_MAX_OUTPUT_SIZE;

            ret = CryptGetHashParam(ctx->mscHash,
                HP_HASHVAL,
                ctx->dgst,
                &retLen,
                0);

            if (ret == 0) {
                xmlSecInternalError2("CryptGetHashParam", xmlSecTransformGetName(transform),
                    "size=" XMLSEC_SIZE_FMT, inSize);
                return(-1);
            }
            xmlSecAssert2(retLen > 0, -1);
            XMLSEC_SAFE_CAST_ULONG_TO_SIZE(retLen, ctx->dgstSize, return(-1), xmlSecTransformGetName(transform));

            /* check/set the result digest size */
            if(ctx->dgstSizeInBits == 0) {
                ctx->dgstSizeInBits = ctx->dgstSize * 8; /* no dgst size specified, use all we have */
            }
            if (transform->operation == xmlSecTransformOperationSign) {
                ret = xmlSecTransformHmacWriteOutput(ctx->dgst, ctx->dgstSizeInBits, ctx->dgstSize, out);
                if (ret < 0) {
                    xmlSecInternalError("xmlSecTransformHmacWriteOutput", xmlSecTransformGetName(transform));
                    return(-1);
                }
            }
            transform->status = xmlSecTransformStatusFinished;
        }
    } else if(transform->status == xmlSecTransformStatusFinished) {
        /* the only way we can get here is if there is no input */
        xmlSecAssert2(xmlSecBufferGetSize(in) == 0, -1);
    } else {
        xmlSecInvalidTransfromStatusError(transform);
        return(-1);
    }

    return(0);
}

#ifndef XMLSEC_NO_MD5
/******************************************************************************
 *
 * HMAC MD5
 *
 ******************************************************************************/
static xmlSecTransformKlass xmlSecMSCryptoHmacMd5Klass = {
    /* klass/object sizes */
    sizeof(xmlSecTransformKlass),               /* xmlSecSize klassSize */
    xmlSecMSCryptoHmacSize,                      /* xmlSecSize objSize */

    xmlSecNameHmacMd5,                          /* const xmlChar* name; */
    xmlSecHrefHmacMd5,                          /* const xmlChar* href; */
    xmlSecTransformUsageSignatureMethod,        /* xmlSecTransformUsage usage; */

    xmlSecMSCryptoHmacInitialize,                /* xmlSecTransformInitializeMethod initialize; */
    xmlSecMSCryptoHmacFinalize,                  /* xmlSecTransformFinalizeMethod finalize; */
    xmlSecMSCryptoHmacNodeRead,                  /* xmlSecTransformNodeReadMethod readNode; */
    NULL,                                       /* xmlSecTransformNodeWriteMethod writeNode; */
    xmlSecMSCryptoHmacSetKeyReq,                 /* xmlSecTransformSetKeyReqMethod setKeyReq; */
    xmlSecMSCryptoHmacSetKey,                    /* xmlSecTransformSetKeyMethod setKey; */
    xmlSecMSCryptoHmacVerify,                    /* xmlSecTransformValidateMethod validate; */
    xmlSecTransformDefaultGetDataType,          /* xmlSecTransformGetDataTypeMethod getDataType; */
    xmlSecTransformDefaultPushBin,              /* xmlSecTransformPushBinMethod pushBin; */
    xmlSecTransformDefaultPopBin,               /* xmlSecTransformPopBinMethod popBin; */
    NULL,                                       /* xmlSecTransformPushXmlMethod pushXml; */
    NULL,                                       /* xmlSecTransformPopXmlMethod popXml; */
    xmlSecMSCryptoHmacExecute,                   /* xmlSecTransformExecuteMethod execute; */

    NULL,                                       /* void* reserved0; */
    NULL,                                       /* void* reserved1; */
};

/**
 * xmlSecMSCryptoTransformHmacMd5GetKlass:
 *
 * The HMAC-MD5 transform klass.
 *
 * Returns: the HMAC-MD5 transform klass.
 */
xmlSecTransformId
xmlSecMSCryptoTransformHmacMd5GetKlass(void) {
    return(&xmlSecMSCryptoHmacMd5Klass);
}

#endif /* XMLSEC_NO_MD5 */


#ifndef XMLSEC_NO_RIPEMD160
/******************************************************************************
 *
 * HMAC RIPEMD160
 *
 ******************************************************************************/
static xmlSecTransformKlass xmlSecMSCryptoHmacRipemd160Klass = {
    /* klass/object sizes */
    sizeof(xmlSecTransformKlass),               /* xmlSecSize klassSize */
    xmlSecMSCryptoHmacSize,                      /* xmlSecSize objSize */

    xmlSecNameHmacRipemd160,                    /* const xmlChar* name; */
    xmlSecHrefHmacRipemd160,                    /* const xmlChar* href; */
    xmlSecTransformUsageSignatureMethod,        /* xmlSecTransformUsage usage; */

    xmlSecMSCryptoHmacInitialize,                /* xmlSecTransformInitializeMethod initialize; */
    xmlSecMSCryptoHmacFinalize,                  /* xmlSecTransformFinalizeMethod finalize; */
    xmlSecMSCryptoHmacNodeRead,                  /* xmlSecTransformNodeReadMethod readNode; */
    NULL,                                       /* xmlSecTransformNodeWriteMethod writeNode; */
    xmlSecMSCryptoHmacSetKeyReq,                 /* xmlSecTransformSetKeyReqMethod setKeyReq; */
    xmlSecMSCryptoHmacSetKey,                    /* xmlSecTransformSetKeyMethod setKey; */
    xmlSecMSCryptoHmacVerify,                    /* xmlSecTransformValidateMethod validate; */
    xmlSecTransformDefaultGetDataType,          /* xmlSecTransformGetDataTypeMethod getDataType; */
    xmlSecTransformDefaultPushBin,              /* xmlSecTransformPushBinMethod pushBin; */
    xmlSecTransformDefaultPopBin,               /* xmlSecTransformPopBinMethod popBin; */
    NULL,                                       /* xmlSecTransformPushXmlMethod pushXml; */
    NULL,                                       /* xmlSecTransformPopXmlMethod popXml; */
    xmlSecMSCryptoHmacExecute,                   /* xmlSecTransformExecuteMethod execute; */

    NULL,                                       /* void* reserved0; */
    NULL,                                       /* void* reserved1; */
};

/**
 * xmlSecMSCryptoTransformHmacRipemd160GetKlass:
 *
 * The HMAC-RIPEMD160 transform klass.
 *
 * Returns: the HMAC-RIPEMD160 transform klass.
 */
xmlSecTransformId
xmlSecMSCryptoTransformHmacRipemd160GetKlass(void) {
    return(&xmlSecMSCryptoHmacRipemd160Klass);
}
#endif /* XMLSEC_NO_RIPEMD160 */

#ifndef XMLSEC_NO_SHA1
/******************************************************************************
 *
 * HMAC SHA1
 *
 ******************************************************************************/
static xmlSecTransformKlass xmlSecMSCryptoHmacSha1Klass = {
    /* klass/object sizes */
    sizeof(xmlSecTransformKlass),               /* xmlSecSize klassSize */
    xmlSecMSCryptoHmacSize,                      /* xmlSecSize objSize */

    xmlSecNameHmacSha1,                         /* const xmlChar* name; */
    xmlSecHrefHmacSha1,                         /* const xmlChar* href; */
    xmlSecTransformUsageSignatureMethod,        /* xmlSecTransformUsage usage; */

    xmlSecMSCryptoHmacInitialize,                /* xmlSecTransformInitializeMethod initialize; */
    xmlSecMSCryptoHmacFinalize,                  /* xmlSecTransformFinalizeMethod finalize; */
    xmlSecMSCryptoHmacNodeRead,                  /* xmlSecTransformNodeReadMethod readNode; */
    NULL,                                       /* xmlSecTransformNodeWriteMethod writeNode; */
    xmlSecMSCryptoHmacSetKeyReq,                 /* xmlSecTransformSetKeyReqMethod setKeyReq; */
    xmlSecMSCryptoHmacSetKey,                    /* xmlSecTransformSetKeyMethod setKey; */
    xmlSecMSCryptoHmacVerify,                    /* xmlSecTransformValidateMethod validate; */
    xmlSecTransformDefaultGetDataType,          /* xmlSecTransformGetDataTypeMethod getDataType; */
    xmlSecTransformDefaultPushBin,              /* xmlSecTransformPushBinMethod pushBin; */
    xmlSecTransformDefaultPopBin,               /* xmlSecTransformPopBinMethod popBin; */
    NULL,                                       /* xmlSecTransformPushXmlMethod pushXml; */
    NULL,                                       /* xmlSecTransformPopXmlMethod popXml; */
    xmlSecMSCryptoHmacExecute,                   /* xmlSecTransformExecuteMethod execute; */

    NULL,                                       /* void* reserved0; */
    NULL,                                       /* void* reserved1; */
};

/**
 * xmlSecMSCryptoTransformHmacSha1GetKlass:
 *
 * The HMAC-SHA1 transform klass.
 *
 * Returns: the HMAC-SHA1 transform klass.
 */
xmlSecTransformId
xmlSecMSCryptoTransformHmacSha1GetKlass(void) {
    return(&xmlSecMSCryptoHmacSha1Klass);
}

#endif /* XMLSEC_NO_SHA1 */

#ifndef XMLSEC_NO_SHA224
/******************************************************************************
 *
 * HMAC SHA224
 *
 ******************************************************************************/
static xmlSecTransformKlass xmlSecMSCryptoHmacSha224Klass = {
    /* klass/object sizes */
    sizeof(xmlSecTransformKlass),               /* xmlSecSize klassSize */
    xmlSecMSCryptoHmacSize,                      /* xmlSecSize objSize */

    xmlSecNameHmacSha224,                       /* const xmlChar* name; */
    xmlSecHrefHmacSha224,                       /* const xmlChar* href; */
    xmlSecTransformUsageSignatureMethod,        /* xmlSecTransformUsage usage; */

    xmlSecMSCryptoHmacInitialize,                /* xmlSecTransformInitializeMethod initialize; */
    xmlSecMSCryptoHmacFinalize,                  /* xmlSecTransformFinalizeMethod finalize; */
    xmlSecMSCryptoHmacNodeRead,                  /* xmlSecTransformNodeReadMethod readNode; */
    NULL,                                       /* xmlSecTransformNodeWriteMethod writeNode; */
    xmlSecMSCryptoHmacSetKeyReq,                 /* xmlSecTransformSetKeyReqMethod setKeyReq; */
    xmlSecMSCryptoHmacSetKey,                    /* xmlSecTransformSetKeyMethod setKey; */
    xmlSecMSCryptoHmacVerify,                    /* xmlSecTransformValidateMethod validate; */
    xmlSecTransformDefaultGetDataType,          /* xmlSecTransformGetDataTypeMethod getDataType; */
    xmlSecTransformDefaultPushBin,              /* xmlSecTransformPushBinMethod pushBin; */
    xmlSecTransformDefaultPopBin,               /* xmlSecTransformPopBinMethod popBin; */
    NULL,                                       /* xmlSecTransformPushXmlMethod pushXml; */
    NULL,                                       /* xmlSecTransformPopXmlMethod popXml; */
    xmlSecMSCryptoHmacExecute,                   /* xmlSecTransformExecuteMethod execute; */

    NULL,                                       /* void* reserved0; */
    NULL,                                       /* void* reserved1; */
};

/**
 * xmlSecMSCryptoTransformHmacSha224GetKlass:
 *
 * The HMAC-SHA224 transform klass.
 *
 * Returns: the HMAC-SHA224 transform klass.
 */
xmlSecTransformId
xmlSecMSCryptoTransformHmacSha224GetKlass(void) {
    return(&xmlSecMSCryptoHmacSha224Klass);
}

#endif /* XMLSEC_NO_SHA224 */

#ifndef XMLSEC_NO_SHA256
/******************************************************************************
 *
 * HMAC SHA256
 *
 ******************************************************************************/
static xmlSecTransformKlass xmlSecMSCryptoHmacSha256Klass = {
    /* klass/object sizes */
    sizeof(xmlSecTransformKlass),               /* xmlSecSize klassSize */
    xmlSecMSCryptoHmacSize,                      /* xmlSecSize objSize */

    xmlSecNameHmacSha256,                       /* const xmlChar* name; */
    xmlSecHrefHmacSha256,                       /* const xmlChar* href; */
    xmlSecTransformUsageSignatureMethod,        /* xmlSecTransformUsage usage; */

    xmlSecMSCryptoHmacInitialize,                /* xmlSecTransformInitializeMethod initialize; */
    xmlSecMSCryptoHmacFinalize,                  /* xmlSecTransformFinalizeMethod finalize; */
    xmlSecMSCryptoHmacNodeRead,                  /* xmlSecTransformNodeReadMethod readNode; */
    NULL,                                       /* xmlSecTransformNodeWriteMethod writeNode; */
    xmlSecMSCryptoHmacSetKeyReq,                 /* xmlSecTransformSetKeyReqMethod setKeyReq; */
    xmlSecMSCryptoHmacSetKey,                    /* xmlSecTransformSetKeyMethod setKey; */
    xmlSecMSCryptoHmacVerify,                    /* xmlSecTransformValidateMethod validate; */
    xmlSecTransformDefaultGetDataType,          /* xmlSecTransformGetDataTypeMethod getDataType; */
    xmlSecTransformDefaultPushBin,              /* xmlSecTransformPushBinMethod pushBin; */
    xmlSecTransformDefaultPopBin,               /* xmlSecTransformPopBinMethod popBin; */
    NULL,                                       /* xmlSecTransformPushXmlMethod pushXml; */
    NULL,                                       /* xmlSecTransformPopXmlMethod popXml; */
    xmlSecMSCryptoHmacExecute,                   /* xmlSecTransformExecuteMethod execute; */

    NULL,                                       /* void* reserved0; */
    NULL,                                       /* void* reserved1; */
};

/**
 * xmlSecMSCryptoTransformHmacSha256GetKlass:
 *
 * The HMAC-SHA256 transform klass.
 *
 * Returns: the HMAC-SHA256 transform klass.
 */
xmlSecTransformId
xmlSecMSCryptoTransformHmacSha256GetKlass(void) {
    return(&xmlSecMSCryptoHmacSha256Klass);
}

#endif /* XMLSEC_NO_SHA256 */

#ifndef XMLSEC_NO_SHA384
/******************************************************************************
 *
 * HMAC SHA384
 *
 ******************************************************************************/
static xmlSecTransformKlass xmlSecMSCryptoHmacSha384Klass = {
    /* klass/object sizes */
    sizeof(xmlSecTransformKlass),               /* xmlSecSize klassSize */
    xmlSecMSCryptoHmacSize,                      /* xmlSecSize objSize */

    xmlSecNameHmacSha384,                       /* const xmlChar* name; */
    xmlSecHrefHmacSha384,                       /* const xmlChar* href; */
    xmlSecTransformUsageSignatureMethod,        /* xmlSecTransformUsage usage; */

    xmlSecMSCryptoHmacInitialize,                /* xmlSecTransformInitializeMethod initialize; */
    xmlSecMSCryptoHmacFinalize,                  /* xmlSecTransformFinalizeMethod finalize; */
    xmlSecMSCryptoHmacNodeRead,                  /* xmlSecTransformNodeReadMethod readNode; */
    NULL,                                       /* xmlSecTransformNodeWriteMethod writeNode; */
    xmlSecMSCryptoHmacSetKeyReq,                 /* xmlSecTransformSetKeyReqMethod setKeyReq; */
    xmlSecMSCryptoHmacSetKey,                    /* xmlSecTransformSetKeyMethod setKey; */
    xmlSecMSCryptoHmacVerify,                    /* xmlSecTransformValidateMethod validate; */
    xmlSecTransformDefaultGetDataType,          /* xmlSecTransformGetDataTypeMethod getDataType; */
    xmlSecTransformDefaultPushBin,              /* xmlSecTransformPushBinMethod pushBin; */
    xmlSecTransformDefaultPopBin,               /* xmlSecTransformPopBinMethod popBin; */
    NULL,                                       /* xmlSecTransformPushXmlMethod pushXml; */
    NULL,                                       /* xmlSecTransformPopXmlMethod popXml; */
    xmlSecMSCryptoHmacExecute,                   /* xmlSecTransformExecuteMethod execute; */

    NULL,                                       /* void* reserved0; */
    NULL,                                       /* void* reserved1; */
};

/**
 * xmlSecMSCryptoTransformHmacSha384GetKlass:
 *
 * The HMAC-SHA384 transform klass.
 *
 * Returns: the HMAC-SHA384 transform klass.
 */
xmlSecTransformId
xmlSecMSCryptoTransformHmacSha384GetKlass(void) {
    return(&xmlSecMSCryptoHmacSha384Klass);
}

#endif /* XMLSEC_NO_SHA384 */

#ifndef XMLSEC_NO_SHA512
/******************************************************************************
 *
 * HMAC SHA512
 *
 ******************************************************************************/
static xmlSecTransformKlass xmlSecMSCryptoHmacSha512Klass = {
    /* klass/object sizes */
    sizeof(xmlSecTransformKlass),               /* xmlSecSize klassSize */
    xmlSecMSCryptoHmacSize,                      /* xmlSecSize objSize */

    xmlSecNameHmacSha512,                       /* const xmlChar* name; */
    xmlSecHrefHmacSha512,                       /* const xmlChar* href; */
    xmlSecTransformUsageSignatureMethod,        /* xmlSecTransformUsage usage; */

    xmlSecMSCryptoHmacInitialize,                /* xmlSecTransformInitializeMethod initialize; */
    xmlSecMSCryptoHmacFinalize,                  /* xmlSecTransformFinalizeMethod finalize; */
    xmlSecMSCryptoHmacNodeRead,                  /* xmlSecTransformNodeReadMethod readNode; */
    NULL,                                       /* xmlSecTransformNodeWriteMethod writeNode; */
    xmlSecMSCryptoHmacSetKeyReq,                 /* xmlSecTransformSetKeyReqMethod setKeyReq; */
    xmlSecMSCryptoHmacSetKey,                    /* xmlSecTransformSetKeyMethod setKey; */
    xmlSecMSCryptoHmacVerify,                    /* xmlSecTransformValidateMethod validate; */
    xmlSecTransformDefaultGetDataType,          /* xmlSecTransformGetDataTypeMethod getDataType; */
    xmlSecTransformDefaultPushBin,              /* xmlSecTransformPushBinMethod pushBin; */
    xmlSecTransformDefaultPopBin,               /* xmlSecTransformPopBinMethod popBin; */
    NULL,                                       /* xmlSecTransformPushXmlMethod pushXml; */
    NULL,                                       /* xmlSecTransformPopXmlMethod popXml; */
    xmlSecMSCryptoHmacExecute,                   /* xmlSecTransformExecuteMethod execute; */

    NULL,                                       /* void* reserved0; */
    NULL,                                       /* void* reserved1; */
};

/**
 * xmlSecMSCryptoTransformHmacSha512GetKlass:
 *
 * The HMAC-SHA512 transform klass.
 *
 * Returns: the HMAC-SHA512 transform klass.
 */
xmlSecTransformId
xmlSecMSCryptoTransformHmacSha512GetKlass(void) {
    return(&xmlSecMSCryptoHmacSha512Klass);
}

#endif /* XMLSEC_NO_SHA512 */


#endif /* XMLSEC_NO_HMAC */
