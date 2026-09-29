/* Application only; requires the board's existing S140 7.3.0 SoftDevice. */
SEARCH_DIR(.)
GROUP(-lgcc -lc -lnosys)
MEMORY {
  FLASH (rx) : ORIGIN = 0x27000, LENGTH = 0x88000 - 0x27000
  /* Bootloader bond handover lives at 0x20007f80; keep it outside app data/heap. */
  RAM (rwx) : ORIGIN = 0x20008000, LENGTH = 0x20040000 - 0x20008000
}
SECTIONS {
  .svc_data : {
    . = ALIGN(4);
    PROVIDE(__start_svc_data = .); KEEP(*(.svc_data)) PROVIDE(__stop_svc_data = .);
  } > RAM
  .fs_data : {
    . = ALIGN(4);
    PROVIDE(__start_fs_data = .); KEEP(*(.fs_data)) PROVIDE(__stop_fs_data = .);
  } > RAM
} INSERT AFTER .data;
INCLUDE "nrf52_common.ld"
