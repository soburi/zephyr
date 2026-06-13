# BCM2712 / RP1 GPIO割り込み ハードウェア仕様メモ

RPi5 (BCM2712 + RP1) のGPIO割り込みをZephyrに実装するにあたって必要となった
ハードウェア仕様のまとめ。一次情報はRaspberry Pi公式Linuxカーネル
(raspberrypi/linux `rpi-6.12.y`) の以下のソースから確認した。

- `drivers/pinctrl/pinctrl-rp1.c` — IOバンクのGPIO/割り込みレジスタ
- `drivers/mfd/rp1.c` — RP1のMSI-X glue (PCIE APBブロック)
- `drivers/irqchip/irq-bcm2712-mip.c` — MIP (MSI→GIC SPI変換器)
- `drivers/pci/controller/pcie-brcmstb.c` — RCのインバウンドウィンドウ設定
- `arch/arm64/boot/dts/broadcom/bcm2712.dtsi` — mip0/mip1、pcie2のranges/dma-ranges
- `include/dt-bindings/mfd/rp1.h` — RP1ペリフェラルのベースアドレスとIRQ番号

## 1. 割り込み経路の全体像

RP1はBCM2712のpcie2配下にぶら下がるPCIeエンドポイントであり、RP1内蔵
ペリフェラルの割り込みはすべてPCIeのMSI-Xメッセージとしてホストに届く。

```
GPIOピンイベント
  → RP1 IOバンク割り込み (CTRL.IRQEN_* で許可、PCIE_INTE/INTSで集約)
  → RP1 MSI-Xベクタ N (IOバンクNはベクタNに固定配線)
  → MSI-Xメモリライト: アドレス 0xff_fffff000 / データ D (ベクタ毎に設定値)
  → PCIe RCのインバウンドウィンドウ (RC BAR4) が MIP制御ブロックへ転送
  → MIPがGIC SPI (msi-base-spi + D) をアサート
  → GIC → CPU
```

- データ値 `D` はホストがMSI-Xテーブルに書き込む任意の値。Linux/本実装とも
  `SPI = 128 + D` となるよう設定する (mip0の `brcm,msi-base-spi = 128`)。
- ZephyrのGIC INTIDは SPI番号+32 (例: SPI 128 → INTID 160)。

## 2. アドレスマップ

3つのアドレス空間が関係する: CPU物理アドレス、PCIeバスアドレス、RP1内部バス
アドレス (RP1ペリフェラルベース 0x40000000)。

### アウトバウンド (CPU→PCI、pcie2)

| CPU物理 | PCIバス | サイズ | 内容 |
|---|---|---|---|
| 0x1f_00000000 | 0x0_00000000 | ~4GiB | 32bit non-prefetchable窓 |
| 0x1c_00000000 | 0x4_00000000 | 12GiB | 64bit prefetchable窓 |

### RP1のBAR割り当て (Zephyrでは固定値で設定)

| BAR | PCIバスアドレス | CPU物理 | サイズ | 内容 |
|---|---|---|---|---|
| BAR0 | 0x410000 | 0x1f_00410000 | 16KiB | MSI-Xテーブル / PBA |
| BAR1 | 0x000000 | 0x1f_00000000 | 4MiB | RP1ペリフェラル空間 (RP1バス0x40000000〜) |

### BAR1内の主要ブロック (RP1バスアドレス下位 = BAR1オフセット)

| オフセット | CPU物理 | ブロック |
|---|---|---|
| 0x0d0000 | 0x1f_000d0000 | io_bank0 (GPIO 0–27) |
| 0x0d4000 | 0x1f_000d4000 | io_bank1 (GPIO 28–33) |
| 0x0d8000 | 0x1f_000d8000 | io_bank2 (GPIO 34–53) |
| 0x0e0000 / 0x0e4000 / 0x0e8000 | 0x1f_000e0000… | sys_rio0/1/2 |
| 0x0f0000 / 0x0f4000 / 0x0f8000 | 0x1f_000f0000… | pads_bank0/1/2 |
| 0x108000 | 0x1f_00108000 | PCIE APBブロック (MSI-X glue) |

### インバウンド (PCI→CPU、pcie2)

| PCIバス | CPU物理 | サイズ | 用途 |
|---|---|---|---|
| 0xff_fffff000 | 0x10_00130000 | 4KiB | MIP0 MSI-Xターゲット (本変更でRC BAR4に追加) |

## 3. RP1 IOバンク (GPIO) レジスタ

### アトミックエイリアス

IOバンク等のRP1ペリフェラルブロックは、レジスタ空間のミラーに書くことで
read-modify-writeなしのビット操作ができる (RP2040と同方式)。

| オフセット | 動作 |
|---|---|
| +0x0000 | 通常 read/write |
| +0x1000 | 書込値のビットをXOR |
| +0x2000 | 書込値のビットをセット |
| +0x3000 | 書込値のビットをクリア |

各IOバンクは16KiBブロック (bank0/1/2 = +0x0000/+0x4000/+0x8000) で、
エイリアスはバンクブロック単位で機能する。

### ピン毎レジスタ (バンク内オフセット = ピン番号×8)

- `STATUS` (+0x0)、`CTRL` (+0x4)

`CTRL` のビット (割り込み関連):

| ビット | 名称 | 内容 |
|---|---|---|
| 4:0 | FUNCSEL | 機能選択 (0x5 = RIO/GPIO) |
| 13:12 / 15:14 / 17:16 | OUTOVER / OEOVER / INOVER | 出力/OE/入力オーバーライド |
| 20 | IRQEN_FALLING | 立下りエッジ検出許可 |
| 21 | IRQEN_RISING | 立上りエッジ検出許可 |
| 22 | IRQEN_LOW | Lowレベル検出許可 |
| 23 | IRQEN_HIGH | Highレベル検出許可 |
| 27:24 | IRQEN_F_* | デバウンスフィルタ後イベントの検出許可 (今回未使用) |
| 28 | IRQRESET | 書込みでラッチ済みエッジイベントをクリア |
| 31:30 | IRQOVER | 割り込み出力のオーバーライド/反転 |

`STATUS` のビット20–23 (FALLING/RISING/LOW/HIGH、rawイベント)、24–27 (フィルタ後)。

### バンク毎の割り込み集約レジスタ (バンクブロック先頭からのオフセット)

| オフセット | 名称 | 内容 |
|---|---|---|
| +0x11c | PCIE_INTE | ピン毎の割り込みイネーブル (bit n = ピンn)。PCIe (MSI-X) 行き |
| +0x124 | PCIE_INTS | ピン毎の割り込みステータス |

注: Linuxのシンボル名は `RP1_GPIO_PCIE_INTE/INTS`。同様のINTE/INTSが
VPU等の他のターゲット向けにも存在するが、ホスト(PCIe)向けはこのオフセット。

## 4. RP1 PCIE APBブロック (MSI-X glue) — BAR1 + 0x108000

RP1内部割り込みソースとMSI-Xベクタの接続を制御する。エイリアスは
**+0x800 (SET) / +0xc00 (CLR)** (IOバンクの0x2000/0x3000とは異なる)。

| オフセット | 名称 | 内容 |
|---|---|---|
| 0x008 + 4×v | MSIX_CFG(v) | ベクタvの制御 |
| 0x108 / 0x10c | INTSTATL / INTSTATH | 割り込みソースの生ステータス (デバッグ用) |

`MSIX_CFG` のビット:

| ビット | 名称 | 内容 |
|---|---|---|
| 0 | ENABLE | ベクタ有効 (ソース→MSI-X接続) |
| 1 | TEST | テスト発火 |
| 2 | IACK | 書込みでアクノリッジ。レベルソースがまだアサート中ならMSIを再送 |
| 3 | IACK_EN | IACK機構の有効化 (レベルトリガソース用) |

レベルトリガのソース (GPIOバンク割り込みもレベル) は、アサート時に一度だけ
MSIが送られる。`IACK_EN` を立てておき、ハンドラ終了時に `IACK` を書くことで
「まだペンディングなら再送」というレベル動作をエッジであるMSI上に再現する。

## 5. RP1 MSI-X仕様

- ベクタ数: 61 (`RP1_INT_END = 61`)。**割り込みソース番号 = ベクタ番号に固定配線**。
  - IO_BANK0 = 0、IO_BANK1 = 1、IO_BANK2 = 2 (以降 AUDIO_IN=3, …)
- MSI-XケーパビリティID 0x11。テーブルBIR = 0 (BAR0)、PBAもBAR0内。
- テーブルエントリ (16バイト/ベクタ、標準形式):

| オフセット | 内容 |
|---|---|
| +0x0 | Message Address下位 (→ 0xfffff000) |
| +0x4 | Message Address上位 (→ 0xff) |
| +0x8 | Message Data (→ MIPに渡る値 D) |
| +0xc | Vector Control (bit0 = マスク。0で有効) |

- Message Control (ケーパビリティ先頭dwordの上位16bit): bit15 = MSI-X Enable、
  bit14 = Function Mask。
- MSIライトを発行するため、RP1のPCIコマンドレジスタで **Bus Master Enable**
  が必要 (従来のZephyrドライバはMemory Space Enableのみだったため追加した)。

## 6. BCM2712 MIP (MSI-X Interrupt Peripheral)

PCIe MSI/MSI-XライトをGIC SPIに変換する。2インスタンスある:

| | 制御ブロック (CPU物理) | PCIターゲット | SPIベース | SPI数 |
|---|---|---|---|---|
| mip0 (pcie2用) | 0x10_00130000 | 0xff_fffff000 | 128 | 64 |
| mip1 | 0x10_00131000 | 0xff_ffffe000※ | 247 | 8 |

※ rpi-6.12.yのdtsiではmip1のターゲットも0xff_fffff000と記載 (RC毎の
インバウンドマッピングで区別されるため衝突しない)。

動作: ターゲットアドレスへのデータ値 `D` の32bitライトで SPI (ベース + D) を
発火。エッジ/レベル等の設定は以下のレジスタで行う (L/Hで64ベクタ分):

| オフセット | 名称 | 内容 |
|---|---|---|
| 0x00 | INT_RAISE | SWからの発火 (デバッグ) |
| 0x10 | INT_CLEAR | クリア |
| 0x20 / 0x30 | INT_CFGL_HOST / CFGH | 1 = エッジトリガ設定 |
| 0x40 / 0x50 | INT_MASKL_HOST / MASKH | ホスト向けマスク (0 = アンマスク) |
| 0x60 / 0x70 | INT_MASKL_VPU / MASKH | VPU向けマスク |
| 0x80 / 0x90 | INT_STATUSL_HOST / STATUSH | ホスト向けステータス |
| 0xa0 / 0xb0 | INT_STATUSL_VPU / STATUSH | VPU向けステータス |

初期化 (Linuxと同一): ホスト全アンマスク、VPU全マスク、全ベクタエッジ設定。
以降のマスク制御はGIC側で行い、MIPは素通し。

## 7. PCIe RC (brcmstb) のインバウンドウィンドウ

BCM2712のPCIeコアはBCM7712系で、インバウンドウィンドウをRC "BAR" レジスタで
複数設定できる。MIPターゲットへのMSIライトを通すには専用窓が1つ必要
(本変更ではRC BAR4を使用)。

| レジスタ | オフセット | 内容 |
|---|---|---|
| RC_BAR4_CONFIG_LO | 0x40d4 | PCIアドレス下位[31:12] \| サイズエンコード[4:0] |
| RC_BAR4_CONFIG_HI | 0x40d8 | PCIアドレス上位32bit |
| UBUS_BAR4_CONFIG_REMAP_LO | 0x410c | CPUアドレス下位[31:12] \| bit0 = ACCESS_EN |
| UBUS_BAR4_CONFIG_REMAP_HI | 0x4110 | CPUアドレス[39:32] |

- サイズエンコード (`encode_ibar_size`): 4KiB–32KiB は `log2(size)-12+0x1c`
  (4KiB → 0x1c)、64KiB以上は `log2(size)-15`。
- BAR1〜3は 0x402c/0x4034/0x403c (LO、+4がHI)、UBUSリマップはBAR4以降
  0x410c + 8×(n-4)。

### エンドポイントのコンフィグ空間アクセス

- `EXT_CFG_INDEX` (RCベース+0x9000) に `bus<<20 | dev<<15 | func<<12` を書き、
  `EXT_CFG_DATA` 窓 (RCベース+0x8000、4KiB) 経由でアクセスする。
  Zephyrの `bdf << 12` はこの形式と一致する。
- 既存Zephyrドライバ(EP BAR割り当て)と同様、INDEX=0 のままで唯一の
  エンドポイントであるRP1のコンフィグ空間に到達できる (実機で動作実績の
  ある経路)。本実装のMSI-Xケーパビリティ操作も同じ経路 (bdf=0) を使う。

## 8. Zephyr実装との対応

RP1のGPIOドライバは rpi_pico 共通ドライバ (`gpio_rpi_pico.c`) と RP1
固有のHAL (`gpio_rp1_hal.h`) に分離されている。割り込みロジックはHAL側
にあり、MSI-X配線もHALの `gpio_rpi_hal_irq_setup()` で行う。

| 仕様 | 実装箇所 |
|---|---|
| IOバンクIRQEN/INTE/INTS、IRQRESET | `drivers/gpio/gpio_rp1_hal.h` (`gpio_set_irq_enabled`, `gpio_get_irq_event_mask`, `gpio_acknowledge_irq`) |
| ISR (INTS走査→ラッチクリア→コールバック) | `drivers/gpio/gpio_rpi_pico.c` (`gpio_rpi_isr`)、bank 0 のみ |
| MSI-Xテーブル/ケーパビリティ、MSIX_CFG、IACK再送 | `gpio_rp1_hal.h` (`gpio_rpi_hal_irq_setup`, `gpio_acknowledge_irq`)。共通ドライバの bank 0 init から `gpio_rpi_hal_irq_setup()` フック経由で呼ぶ (Pico HALはno-op) |
| MIP初期化 | `drivers/interrupt_controller/intc_bcm2712_mip.c` |
| RC BAR4インバウンド窓、EPのBus Master Enable | `drivers/pcie/controller/pcie_brcmstb.c` |
| アドレス/SPI番号の定義 | `dts/arm64/broadcom/bcm2712.dtsi` (mip0、pcie2 ranges[3]、rp1 pinctrl の interrupts/msi-parent) |
| MSIデータ値の導出 | `D = DT_IRQN(pinctrl) − GIC_SPI_INT_BASE − mip0のbrcm,msi-base-spi`。GPIO割り込みは bank 0 (MSI-Xベクタ0) のみ配線 |
| PCIE APB/MSI-Xテーブルの物理アドレス | `gpio_rp1_hal.h` に固定値 (BAR1+0x108000 / BAR0)。RP1のBARはbrcmstb PCIeドライバが固定構成で割り当てる |

## 9. 情報源の分類

BCM2712には公開データシートが存在せず、RP1のデータシート(公開draft
"Raspberry Pi RP1 Peripherals")にはPCIEブロックの章が無い。そのため
「割り込みがPCIeを渡る区間」の両端 (RP1のMSI-X glueとBCM2712のMIP/RC
インバウンド窓) は **Linuxソースが事実上唯一の公開情報源** である。

### Linuxソースからのみ判別できる仕様

| 仕様 | 唯一の情報源 |
|---|---|
| MIPの存在と全仕様 (制御レジスタマップ 0x00〜0xb0、データ値→SPI変換のセマンティクス、初期化手順) | `drivers/irqchip/irq-bcm2712-mip.c` |
| MIPのアドレス類 (制御ブロック 0x10_00130000/131000、PCIターゲット 0xff_fffff000、SPIベース 128/247 と本数 64/8) | `arch/arm64/boot/dts/broadcom/bcm2712.dtsi` |
| MIPターゲットへのインバウンド窓が必要という構成 (PCI 0xff_fffff000 → MIP制御ブロック) | 同dtsiの `dma-ranges` |
| RP1 PCIE APBブロック (ベース 0x108000、MSIX_CFG(v)=0x008+4v、ENABLE/TEST/IACK/IACK_ENビット、SET/CLRエイリアスが +0x800/+0xc00、INTSTATL/H) | `drivers/mfd/rp1.c`、`include/dt-bindings/mfd/rp1.h` |
| レベル割り込みのIACK再送セマンティクス (レベルソースはアサート時に1回だけMSI送出、IACK書込みでペンディング中なら再送) | `drivers/mfd/rp1.c` の `level_triggered_irq` 処理 |
| BCM2712 PCIe RCの非標準レジスタ (RC_BARn_CONFIG_LO/HI、UBUS_BARn_CONFIG_REMAP、サイズエンコード、BCM2712が7712系でウィンドウ毎のUBUSリマップ必須なこと、EXT_CFG_INDEXのフォーマット) | `drivers/pci/controller/pcie-brcmstb.c` (Broadcom STB系資料は非公開) |
| pcie2配下にRP1、msi-parent=mip0というシステム接続関係 | `bcm2712.dtsi` |

### RP1公式データシート (draft) で確認可能

- IOバンクのアドレスマップ (0xd0000〜)
- GPIO STATUS/CTRLレジスタ: IRQEN (bit20–23)、IRQRESET (bit28)
- バンク毎の PCIE_INTE/INTS (+0x11c/+0x124)
- アトミックエイリアス (+0x1000/0x2000/0x3000)
- 割り込みソース番号 (IO_BANK0/1/2 = 0/1/2) とMSI-X 61本

### PCIe標準仕様で判別可能

- MSI-Xケーパビリティ (ID 0x11) の構造、Message Control の
  Enable/Function Mask ビット
- テーブルエントリ形式 (16バイト) と Vector Control のマスクビット
- BIRによるテーブル位置の特定
- MSI送出にBus Master Enableが必要な点

### どちらにも無い経験則 (Linux由来でもない)

- EXT_CFG INDEX=0 のままRP1のコンフィグ空間に到達できる点。既存Zephyr
  ドライバ (EP BAR割り当て) の実機動作実績によるもので、Linuxはバス番号を
  正しくINDEXに設定する正攻法を使っている。
