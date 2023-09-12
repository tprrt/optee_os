global-incdirs-y += .

at91-common = at91_sckc.c at91_main.c at91_pmc.c
at91-common += at91_master.c
at91-common += at91_programmable.c at91_system.c at91_peripheral.c
at91-common += at91_generated.c

srcs-$(CFG_DRIVERS_SAMA5D2_CLK) += at91_pll.c at91_plldiv.c
srcs-$(CFG_DRIVERS_SAMA5D2_CLK) += at91_utmi.c at91_h32mx.c at91_usb.c
srcs-$(CFG_DRIVERS_SAMA5D2_CLK) += at91_i2s_mux.c at91_audio_pll.c
srcs-$(CFG_DRIVERS_SAMA5D2_CLK) += $(at91-common) sama5d2_clk.c

srcs-$(CFG_DRIVERS_SAMA7G5_CLK) += $(at91-common) sama7g5_clk.c clk-sam9x60-pll.c
