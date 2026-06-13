# BCM2712 / RP1 GPIO割り込み ハードウェア仕様メモ

RPi5 (BCM2712 + RP1) のGPIO割り込みをZephyrに実装するにあたって必要となった
ハードウェア仕様のまとめ。GPLソースへの依存を減らすため、確認元は以下の順に
優先した。

- Raspberry Pi Ltd, "Raspberry Pi RP1 Peripherals" (RP-008370-DS-1,
  build 2023-11-07) — RP1の公開データシート
- smallkirby, "自作OS on Raspberry Pi 5 - Part 3. RP1 MSI-X"
  (`smallkirby/zenn`, MIT license) — RPi5実機で観測されたRP1 MSI-X
  capability/table、MIP0 target、inbound translation、MSIX_CFG設定
- OpenBSD `src` — BCM2712/RP1対応ドライバ (ISCライセンス)
  - `sys/arch/arm64/dev/bcm2712_mip.c` — MIP
  - `sys/dev/fdt/bcm2711_pcie.c` — BCM2711/BCM2712 PCIe RC
  - `sys/arch/arm64/dev/rpone.c` — RP1 PCI function / MSI-X glue
  - `sys/arch/arm64/dev/rpigpio.c` — RP1 GPIO/pinctrl
- PCI Express Base Specification — 標準MSI-X capability/table形式
- Raspberry Pi公式Linuxカーネル (`raspberrypi/linux` `rpi-6.12.y`) —
  Raspberry Pi 5固有のDT接続情報と割り込みソース番号表の照合用

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

- データ値 `D` はホストがMSI-Xテーブルに書き込む任意の値。本実装では
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

初期化 (OpenBSD/Linuxと同一): ホスト全アンマスク、VPU全マスク、全ベクタエッジ設定。
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

BCM2712には公開データシートが存在しないが、MIPとPCIe RCについてはOpenBSDの
実装が独立した公開ソースとして使える。RP1側は公式データシートにGPIOとPCIe
endpoint controllerの章があり、RPi5上でのMSI-X経路はsmallkirby/Urthr記事で
実機観測として整理されている。そのため、Linux依存は主にRaspberry Pi 5固有の
DT接続情報と割り込みソース番号表の照合に限定できる。

### smallkirby/Urthr記事で確認可能

記事ソースは `smallkirby/zenn` リポジトリにあり、同リポジトリはMIT license。
Linuxドライバの実装表現ではなく、PCI config space・MSI-X table・実機動作から
見える外部挙動を根拠として扱う。

| 仕様 | 記事から使える外部挙動 |
|---|---|
| RP1 peripheral interruptの配送モデル | RP1側peripheral interruptはPCIe MSI-X Memory WriteとしてSoC側へ届き、MIPがGIC SPIに変換する |
| MSI-X capability/table | RP1のMSI-X tableはBAR0 offset 0、PBAはBAR0 offset 0x2000、table sizeは61 entries |
| MIP0 target | MSI-X Message AddressはPCIe-visible MIP0 address 0xff_fffff000、CPU/AXI側は0x10_00130000、size 0x1000 |
| MSI data mapping | message data `d` が SPI 128 + d に対応し、GIC INTIDとしては 128 + d + 32 |
| Inbound translation | RC側に PCIe 0xff_fffff000 -> AXI 0x10_00130000, size 0x1000 の変換が必要 |
| RP1 MSIX_CFG | BAR1 + 0x00108000、MSIX_CFG[n] = base + 0x08 + 4*n、64 entries、ENABLE/IACK/IACK_ENの意味 |
| 実機確認 | Ethernet vector 6、data 6でGIC INTID 166としてARP受信通知が観測されている |

この記事だけで足りないのは、MIPの全レジスタマップ、MIP初期化手順の完全な根拠、
BCM2712 PCIe RCの具体的なレジスタプログラミング、GPIO bank interruptのvector番号
である。これらはRP1データシート、OpenBSD、Raspberry Pi DTで補う。

### RP1公式データシートで確認可能

- RP1内部ペリフェラルアドレスマップ:
  - io_bank0/1/2 = 0x400d0000/0x400d4000/0x400d8000
  - sys_rio0/1/2 = 0x400e0000/0x400e4000/0x400e8000
  - pads_bank0/1/2 = 0x400f0000/0x400f4000/0x400f8000
  - pcie = 0x40108000
- アトミックエイリアス (+0x1000/0x2000/0x3000)
- GPIO STATUS/CTRLレジスタ:
  - STATUS bit20–23/24–27 のraw/filtered edge/level event
  - CTRL bit20–27 のIRQ mask、bit28 IRQRESET、bit31:30 IRQOVER
- GPIO interrupt集約:
  - edge interruptはSTATUSに保持され、CTRL.IRQRESETへの1書込みでクリア
  - level interruptはラッチされない
  - Proc0/Proc1/PCIe向けのtop-level enable/status/force registerがある
  - PCIE_INTE/INTF/INTS = +0x11c/+0x120/+0x124
- PCIe endpoint controller:
  - RP1はDesignWare PCIe Endpoint Controller v5.30aを持つ
  - 3本の32bit non-prefetchable BAR、integrated MSI-X capability
  - level-sensitive interrupt sourceからMSI-Xを生成するvector configuration block
  - PCIE_CFG MSIX_CFG_0..63 = +0x008..+0x104
  - MSIX_CFG bit0 ENABLE、bit1 TEST、bit2 IACK、bit3 IACK_EN
  - INTSTATL/INTSTATH = +0x108/+0x10c

注: データシートはPCIE_CFGレジスタをRP1内部アドレス 0x40108000 として記載する。
Zephyr実装ではRP1 BAR1が内部0x40000000基準でホストに見えるため、PCIE APBは
BAR1 + 0x108000 としてアクセスする。

### OpenBSDソースで確認可能

OpenBSDの該当ドライバはGPLではなくISCライセンスなので、Linux由来情報を置き換える
根拠として使える。

| 仕様 | OpenBSD実装 |
|---|---|
| MIPレジスタマップ 0x00〜0xb0、VPU mask、HOST unmask、edge設定 | `sys/arch/arm64/dev/bcm2712_mip.c` |
| MIPのMSI target addressをDT `reg[1]` から取り、MSI dataをGIC SPI offsetとして扱うこと | `bcm2712_mip.c` (`bcmmip_intr_establish_msi`) |
| BCM2712 PCIe inbound windowのRC_BAR/UBUS_REMAP設定、サイズエンコード | `sys/dev/fdt/bcm2711_pcie.c` (`bcmpcie_setup_inbound`) |
| BCM2712 PCIe config access: `EXT_CFG_INDEX = bus<<20 | dev<<15 | func<<12`、`EXT_CFG_DATA`経由 | `bcm2711_pcie.c` |
| RP1 PCI functionがMSI-X vectorsを持ち、子FDT割り込みをMSI-X vectorに対応付けること | `sys/arch/arm64/dev/rpone.c` |
| RP1 MSIX_CFG offset、SET/CLR alias、ENABLE/IACK/IACK_EN、level interrupt ack | `rpone.c` |
| RP1 GPIO bank配置、GPIO CTRL/STATUS bitの独立確認 | `sys/arch/arm64/dev/rpigpio.c` |

### PCIe標準仕様で判別可能

- MSI-Xケーパビリティ (ID 0x11) の構造、Message Control の
  Enable/Function Mask ビット
- テーブルエントリ形式 (16バイト) と Vector Control のマスクビット
- BIRによるテーブル位置の特定
- MSI送出にBus Master Enableが必要な点

### LinuxソースまたはRaspberry Pi DTでまだ補う仕様

| 仕様 | 補足 |
|---|---|
| `pcie2`配下にRP1があり、`mip0`をMSI parentとして使うボード接続 | Raspberry Pi firmware DT / Linux `bcm2712.dtsi` / Zephyr dtsiで確認する接続情報 |
| mip0/mip1の実アドレス、PCI target address、SPI base/count | OpenBSDもDTから読むため、最終的な数値はRaspberry Pi DT由来。Zephyrでは `bcm2712.dtsi` に記述 |
| RP1 interrupt source番号とMSI-X vector番号の完全な対応表 | RP1データシートはMSIX_CFG_0..63を定義するが、IO_BANK0=0などのソース番号表はDT binding/DTで確認する |
| Zephyr実装でMIP用inbound windowをRC BAR4に置く判断 | OpenBSDは`dma-ranges`をBAR1以降へ順に展開する汎用実装。Zephyrの既存BAR1〜3利用状況に合わせ、追加窓としてBAR4を使う |

### Linux由来でもない経験則

- EXT_CFG INDEX=0 のままRP1のコンフィグ空間に到達できる点。既存Zephyr
  ドライバ (EP BAR割り当て) の実機動作実績によるもので、Linuxはバス番号を
  正しくINDEXに設定する正攻法を使っている。
