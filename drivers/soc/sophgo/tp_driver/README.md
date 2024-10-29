# bm1690

## 编译bm1690 tpu driver
make CHIP=bm1690

## bm1690 dtb example for sgtpu
	sgtpu {
		compatible = "sophgo,tpu-1690";
		reg = <0x70 0x50000000 0x0 0x8000>,
			<0x69 0x08050000 0x0 0x3000>, 	// TPSYS0_SYS_REG, size should be 0C00
			<0x69 0x18050000 0x0 0x3000>,
			<0x69 0x28050000 0x0 0x3000>,
			<0x69 0x38050000 0x0 0x3000>,
			<0x69 0x48050000 0x0 0x3000>,
			<0x69 0x58050000 0x0 0x3000>,
			<0x69 0x68050000 0x0 0x3000>,
			<0x69 0x78050000 0x0 0x3000>;
		sophgo_fw_mode = <1>;
	};

# bm1686

## 编译bm1686 tpu driver
make CHIP=bm1686

## bm1686 dtb example for sgtpu
	sgtpu {
		compatible = "sophgo,tpu-1684";
		reg = <0x0 0x50010000 0x0 0x300000>,
			<0x0 0x58000000 0x0 0x10000>;
		sophgo_fw_mode = <FW_SOC_MODE>;
		resets = <&rst RST_SECOND_AP>, <&rst RST_GDMA>, <&rst RST_TPU>,
			<&rst RST_CDMA>, <&rst RST_MMU>;
		reset-names = "arm9", "gdma", "tpu", "cdma", "smmu";
		clocks = <&div_clk GATE_CLK_TPU>, <&div_clk GATE_CLK_GDMA>,
			<&div_clk GATE_CLK_AXI8_CDMA>, <&div_clk GATE_CLK_AXI8_MMU>,
			<&div_clk GATE_CLK_AXI8_PCIE>, <&div_clk GATE_CLK_FIXED_TPU_CLK>,
			<&div_clk GATE_CLK_APB_INTC>, <&div_clk GATE_CLK_ARM>,
			<&div_clk GATE_CLK_AXISRAM>, <&div_clk GATE_CLK_HAU_NGS>,
			<&div_clk GATE_CLK_TPU_FOR_TPU_ONLY>;
		clock-names = "tpu", "gdma", "axi8_cdma", "axi8_mmu", "axi8_pcie", "fixed_tpu_clk",
			"apb_intc", "arm9", "axi_sram", "clk_gate_hau_ngs", "clk_gate_tpu_for_tpu_only";
	};
