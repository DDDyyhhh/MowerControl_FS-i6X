#include "exp3_auto_session.h"
#include <assert.h>
#include <stdio.h>
/* Independent legacy v35 CRC oracle: its handshake had no version salt. */
static unsigned legacy_crc(unsigned nonce, unsigned seed) {
    unsigned crc=seed;
    for(unsigned i=0;i<4U;++i) {
        crc^=(nonce>>(8U*i))&255U;
        for(unsigned bit=0;bit<8U;++bit)
            crc=((crc<<1)^((crc&128U)?7U:0U))&255U;
    }
    return crc;
}
int main(void) {
    Exp3AutoSession receiver, sender;
    exp3_auto_init(&receiver,EXP3_AUTO_CONTROLLER,456U,0U);
    exp3_auto_init(&sender,EXP3_AUTO_HMI,123U,0U);
    const unsigned seeds[]={0x5aU,0x5aU ^ EXP3_AUTO_HMI ^ 0x36U,0x5aU ^ EXP3_AUTO_HMI ^ 0x37U,0x5aU ^ EXP3_AUTO_HMI ^ 0x38U};
    for(unsigned version=0;version<4U;++version) {
        exp3_auto_init(&receiver,EXP3_AUTO_CONTROLLER,456U,0U);
        for(unsigned slot=0;slot<5U;++slot) {
            unsigned value=slot<4U?(123U>>(slot*8U))&255U:legacy_crc(123U,seeds[version]);
            assert(!exp3_auto_receive(&receiver,(uint16_t)(0xc000U|(slot<<8)|value),slot*20U,5000U));
        }
        assert(!receiver.peer);
    }
    int received=0;
    for(unsigned slot=0;slot<5U;++slot)
        received+=exp3_auto_receive(&receiver,exp3_auto_word(&sender,slot*20U,20U,2000U),200U+slot*20U,5000U);
    assert(received==1 && receiver.peer==123U);
    puts("PASS v39 handshake accepts paired version and rejects v35/v36/v37/v38 handshake");
}
