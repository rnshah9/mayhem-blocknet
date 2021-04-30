// Copyright (c) 2017-2019 The Blocknet developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//******************************************************************************
//******************************************************************************

#include <xbridge/util/logger.h>
#include <xbridge/xbridgepacket.h>

#include <crypto/sha256.h>
#include <random.h>
#include <secp256k1.h>
#include <support/allocators/secure.h>
#include "uint256.h"

//******************************************************************************
//******************************************************************************
namespace
{
secp256k1_context * secpContext = nullptr;

class SecpInstance
{
public:
    SecpInstance()
    {
        assert(secpContext == nullptr);

        secp256k1_context * ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);
        assert(ctx != NULL);

        {
            // Pass in a random blinding seed to the secp256k1 context.
            std::vector<unsigned char, secure_allocator<unsigned char>> vseed(32);
            GetRandBytes(vseed.data(), 32);
            bool ret = secp256k1_context_randomize(ctx, vseed.data());
            assert(ret);
        }

        secpContext = ctx;
    }
    ~SecpInstance()
    {
        secp256k1_context * ctx = secpContext;
        secpContext = nullptr;

        if (ctx)
        {
            secp256k1_context_destroy(ctx);
        }
    }
};
static SecpInstance secpInstance;

} // namespace

//******************************************************************************
//******************************************************************************
bool XBridgePacket::sign(const std::vector<unsigned char> & pubkey,
                         const std::vector<unsigned char> & privkey)
{
    if (pubkey.size() != pubkeySize || privkey.size() != privkeySize)
    {
        LOG() << "incorrect key size " << __FUNCTION__;
        return false;
    }

    memcpy(pubkeyField(), &pubkey[0], pubkeySize);
    memset(signatureField(), 0, rawSignatureSize);

    unsigned char hash[CSHA256::OUTPUT_SIZE];

    {
        CSHA256 sha256;
        sha256.Write(&m_body[0], m_body.size());
        sha256.Finalize(hash);
    }

    secp256k1_ecdsa_signature sig;
    if (secp256k1_ecdsa_sign(secpContext, &sig, hash, &privkey[0], 0, 0) == 0)
    {
        return false;
    }

    secp256k1_ecdsa_signature_serialize_compact(secpContext, signatureField(), &sig);

    // TODO verify signature
    return verify();
}

//******************************************************************************
// verify signature
//******************************************************************************
bool XBridgePacket::verify()
{
    unsigned char signature[rawSignatureSize];
    memcpy(signature, signatureField(), rawSignatureSize);
    memset(signatureField(), 0, rawSignatureSize);

    unsigned char hash[CSHA256::OUTPUT_SIZE];

    {
        CSHA256 sha256;
        sha256.Write(&m_body[0], m_body.size());
        sha256.Finalize(hash);
    }

    // restore signature
    memcpy(signatureField(), signature, rawSignatureSize);

    secp256k1_ecdsa_signature sig;
    if (secp256k1_ecdsa_signature_parse_compact(secpContext, &sig, signatureField()) == 0)
    {
        LOG() << "incorrect or unparseable signature " << __FUNCTION__;
        return false;
    }

    secp256k1_pubkey scpubkey;
    if (secp256k1_ec_pubkey_parse(secpContext, &scpubkey, pubkeyField(), pubkeySize) == 0)
    {
        LOG() << "the public key could not be parsed or is invalid " << __FUNCTION__;
        return false;
    }

    if (secp256k1_ecdsa_verify(secpContext, &sig, hash, &scpubkey) != 1)
    {
        LOG() << "bad signature " << __FUNCTION__;
        return false;
    }

    // correct signature, check pubkey
    unsigned char pub[pubkeySize];
    size_t len = pubkeySize;
    secp256k1_ec_pubkey_serialize(secpContext, pub, &len, &scpubkey, SECP256K1_EC_COMPRESSED);

    if (memcmp(pub, pubkeyField(), pubkeySize))
    {
        LOG() << "signature correct, but different pubkeys " << __FUNCTION__;
        return false;
    }
    if (len != pubkeySize)
    {
        LOG() << "incorrect pubkey lengtn returned " << __FUNCTION__;
        return false;
    }

    // all correct
    return true;
}

//******************************************************************************
// verify signature and pubkey
//******************************************************************************
bool XBridgePacket::verify(const std::vector<unsigned char> & pubkey)
{
    if (pubkey.size() != pubkeySize || memcmp(pubkeyField(), &pubkey[0], pubkeySize))
    {
        return false;
    }

    return verify();
}

//******************************************************************************
//******************************************************************************
size_t XBridgePacket::read(const size_t offset, unsigned char * data, const size_t size) const
{
    if (offset + size > this->size() || size == 0)
    {
        LOG() << "wrong packet size " << __FUNCTION__;
        return 0;
    }
    memcpy(data, this->data() + offset, size);
    return offset + size;
}

//******************************************************************************
//******************************************************************************
size_t XBridgePacket::read(const size_t offset, xbridge::amount_t & data) const
{
    // return read(offset, reinterpret_cast<unsigned char>(&data), sizeof(data));
    uint128 tmp;
    size_t tsize = read(offset, tmp.begin(), sizeof(data));
    data = UintToArith128(tmp);
    return tsize;
}

//******************************************************************************
//******************************************************************************
size_t XBridgePacket::read(const size_t offset, uint256 & data) const
{
    return read(offset, data.begin(), sizeof(data));
}

//******************************************************************************
//******************************************************************************
size_t XBridgePacket::read(const size_t offset, std::vector<unsigned char> & data, const size_t size) const
{
    data.resize(size, 0);
    return read(offset, &data[0], size);
}

//******************************************************************************
//******************************************************************************
size_t XBridgePacket::read(const size_t offset, std::string & data, const size_t size) const
{
    data.resize(size, ' ');
    return read(offset, reinterpret_cast<unsigned char *>(&data[0]), size);
}
