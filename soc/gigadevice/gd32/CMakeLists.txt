set(SOC_LINKER_SCRIPT
    ${ZEPHYR_BASE}/include/zephyr/arch/riscv/common/linker.ld
    CACHE INTERNAL "")

zephyr_linker_sources(ROM_START SORT_KEY 0x0 gd32vf103/init.ld)

zephyr_sources(gd32vf103/entry.S)
