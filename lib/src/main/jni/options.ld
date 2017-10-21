SECTIONS {
  /DISCARD/ : {
    *(.note.gnu.gold-version)
    *(.comment)
  }
}
INSERT AFTER .data;