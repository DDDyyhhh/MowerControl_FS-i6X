#include "exp3_completion.h"
#include "exp3_probe.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    const unsigned orders[6][3]={{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
    Exp3Message m={.controller=0x11223344U,.hmi=0x55667788U,.request=321U,
        .action=321U,.generation=654U,.context=EXP3_PWM_CONTEXT,.target=1U,
        .kind=EXP3_RESULT,.sender=2U,.command=EXP3_CONFIRM,.status=EXP3_ACCEPTED};
    uint8_t bytes[9], full[36], actual[36];
    Exp3Message out;
    /* Frozen independently with Python struct.pack + zlib.crc32. */
    const uint8_t golden[9]={0x41U,0x01U,0x8eU,0x02U,0x05U,0xb0U,0x8eU,0x8cU,0xd4U};
    assert(exp3_completion_encode(&m,bytes) && !memcmp(bytes,golden,sizeof(golden)));
    assert(exp3_completion_part(bytes,0x17U,0U)==0x8e014117U);
    for(unsigned context=0;context<2;context++)
      for(unsigned command=EXP3_CONFIRM;command<=EXP3_QUERY;command++)
        for(unsigned status=EXP3_ACCEPTED;status<=EXP3_SESSION;status++) {
          m.context=context?EXP3_UNLOCK_CONTEXT:EXP3_PWM_CONTEXT;
          m.command=command; m.status=status;
          assert(exp3_completion_encode(&m,bytes));
          assert(exp3_encode(&m,full));
          assert(exp3_completion_decode(bytes,m.controller,m.hmi,&out));
          assert(exp3_encode(&out,actual) && !memcmp(full,actual,36U));
          assert(!exp3_completion_decode(bytes,m.controller+1U,m.hmi,&out));
          assert(!exp3_completion_decode(bytes,m.controller,m.hmi+1U,&out));
          for(unsigned bit=0;bit<72;bit++) {
              uint8_t bad[9]; memcpy(bad,bytes,9); bad[bit/8]^=1U<<(bit%8);
              assert(!exp3_completion_decode(bad,m.controller,m.hmi,&out));
          }
          for(unsigned order=0;order<6;order++) {
              Exp3CompletionRx rx={0};
              for(unsigned i=0;i<3;i++) {
                  unsigned p=orders[order][i];
                  uint32_t w=exp3_completion_part(bytes,255U,p);
                  assert(exp3_completion_receive(&rx,p,w,100U+i,m.controller,m.hmi,&out)==
                      (i==2?EXP3_COMPLETE:EXP3_MORE));
                  assert(exp3_completion_receive(&rx,p,w,103U+i,m.controller,m.hmi,&out)==EXP3_DUPLICATE);
              }
              assert(exp3_completion_receive(&rx,0,exp3_completion_part(bytes,1U,0),110U,m.controller,m.hmi,&out)==EXP3_MORE);
              assert(exp3_completion_receive(&rx,1,exp3_completion_part(bytes,255U,1),111U,m.controller,m.hmi,&out)==EXP3_REJECT);
              assert(exp3_completion_receive(&rx,2,exp3_completion_part(bytes,1U,2),112U,m.controller,m.hmi,&out)==EXP3_MORE);
              assert(exp3_completion_receive(&rx,1,exp3_completion_part(bytes,1U,1),113U,m.controller,m.hmi,&out)==EXP3_COMPLETE);
          }
        }
    Exp3CompletionRx rx={0};
    assert(exp3_completion_receive(&rx,0,exp3_completion_part(bytes,3U,0),0xfffffff0U,m.controller,m.hmi,&out)==EXP3_MORE);
    assert(exp3_completion_receive(&rx,1,exp3_completion_part(bytes,3U,1),0x10U,m.controller,m.hmi,&out)==EXP3_MORE);
    assert(exp3_completion_receive(&rx,2,exp3_completion_part(bytes,3U,2),0x20U,m.controller,m.hmi,&out)==EXP3_COMPLETE);
    rx=(Exp3CompletionRx){0};
    assert(exp3_completion_receive(&rx,0,exp3_completion_part(bytes,3U,0),100U,m.controller,m.hmi,&out)==EXP3_MORE);
    assert(exp3_completion_receive(&rx,1,exp3_completion_part(bytes,3U,1),1100U,m.controller,m.hmi,&out)==EXP3_MORE);
    assert(exp3_completion_receive(&rx,2,exp3_completion_part(bytes,3U,2),1101U,m.controller,m.hmi,&out)==EXP3_MORE);
    assert(exp3_completion_receive(&rx,3,1U,1102U,m.controller,m.hmi,&out)==EXP3_REJECT);
    assert(exp3_completion_receive(&rx,0,0U,1102U,m.controller,m.hmi,&out)==EXP3_REJECT);
    m.action=m.request=65536U; assert(!exp3_completion_encode(&m,bytes));
    m.action=m.request=321U; m.generation=65536U; assert(!exp3_completion_encode(&m,bytes));
    puts("PASS completion codec: full canonical CRC/identity, all contexts/results/orders, corruption, mixed publication, expiry/wrap, wide fallback");
}
