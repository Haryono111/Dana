# See LICENSE.ibm for license details.

#*****************************************************************************
# csr.S
#-----------------------------------------------------------------------------
#
# Check that we can read and write CSRs
#

#include "riscv_test.h"
#include "include/test_macros.h"
#include "include/xfiles-dana.h"
#include "rocc-software/src/xcustom.h"
#include "rocc-software/src/riscv_test_rocc.h"
#include "libs/src/xfiles-supervisor.S"
#include "libs/src/xfiles-user.S"

#define CUSTOM_X 0
#define STACK_TOP (_end + 4096)
#ifndef ID_STRING
#define ID_STRING 0x0002000000001012
// * Transaction Table Entries: 2 [63:48]
// * Elements Per Block:        4 [13:10]
// * Processing Elements:       1 [ 9: 4]
// * Cache Entries:             2 [ 3: 0]
#endif
#define ID_STRING_CURRENT 0x0001000000001001

// RVTEST_CODE_BEGIN includes the EXTRA_INIT macro before its final
// `mret` and the resulting drop to user mode. We use this to setup
// the ASID and ANTP for a single transaction test.
#undef EXTRA_INIT
#define EXTRA_INIT                              \
  XF_WRITE_CSR(csr_TTABLE_SIZE,  0x1);          \
  XF_WRITE_CSR(csr_ASID,         0x14);         \
  XF_WRITE_CSR(csr_TID,          0x15);         \
  XF_WRITE_CSR(csr_DEBUG,        0x1);          \
  XF_WRITE_CSR(csr_PE_SIZE,      0x0);          \
  XF_WRITE_CSR(csr_CACHE_SIZE,   0x1);          \
  XF_WRITE_CSR(csr_PE_COOLDOWN,  0x7);          \
  XF_WRITE_CSR(csr_ANTP,         0x103);        \
  XF_WRITE_CSR(csr_NUM_ASIDS,    0x104);        \
  XF_WRITE_CSR(csr_PE_GOVERNOR,  0x1);          \
  la sp, _end + 1024;

RVTEST_WITH_ROCC

start:

RVTEST_CODE_BEGIN
  TEST_CASE(  1, a0, 0x1,               XF_READ_CSR(csr_TTABLE_SIZE)  );
  TEST_CASE(  2, a0, ID_STRING,         XF_READ_CSR(csr_XFID)         );
  TEST_CASE(  3, a0, ID_STRING_CURRENT, XF_READ_CSR(csr_XFID_CURRENT) );
  TEST_CASE(  4, a0, 0x14,              XF_READ_CSR(csr_ASID)         );
  TEST_CASE(  5, a0, 0x15,              XF_READ_CSR(csr_TID)          );
  TEST_CASE(  6, a0, 0x1,               XF_READ_CSR(csr_DEBUG)        );

  TEST_CASE( 10, a0, 0x0,               XF_READ_CSR(csr_PE_SIZE)      );
  TEST_CASE( 11, a0, 0x1,               XF_READ_CSR(csr_CACHE_SIZE)   );
  TEST_CASE( 12, a0, 0x7,               XF_READ_CSR(csr_PE_COOLDOWN)  );
  TEST_CASE( 13, a0, 0x103,             XF_READ_CSR(csr_ANTP)         );
  TEST_CASE( 14, a0, 0x104,             XF_READ_CSR(csr_NUM_ASIDS)    );
  TEST_CASE( 15, a0, 0x1,               XF_READ_CSR(csr_PE_GOVERNOR)  );

  XF_WRITE_CSR(csr_TID, 0x20);
  TEST_CASE( 20, a0, 0x20,              XF_READ_CSR(csr_TID));

  TEST_PASSFAIL

RVTEST_CODE_END

  .data
RVTEST_DATA_BEGIN

  TEST_DATA

RVTEST_DATA_END