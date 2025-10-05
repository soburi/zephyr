# M5Stack CoreS3 Build Notes

このメモでは、`m5stack_cores3/esp32s3/procpu` ボード向けに Zephyr をビルドする際に必要となった追加ステップと、現状の制限点を整理する。

## 事前準備

1. 仮想環境を有効化する。
   ```sh
   source .venv/bin/activate
   ```
2. ネイティブビルドで必要になる 32bit ライブラリと `file` コマンドをインストールする。
   ```sh
   sudo apt-get update
   sudo apt-get install -y gcc-multilib file
   ```
   これにより `west build -b native_sim ...` が通るようになる。

## Zephyr SDK の整備

1. ホストツールをインストールする。
   ```sh
   /root/zephyr-sdk-0.17.4/setup.sh -h
   ```
   対話モードの場合は、最後の `Install host tools` で `y` を選択する。

2. ESP32-S3 用ツールチェーンを追加する。
   ```sh
   /root/zephyr-sdk-0.17.4/setup.sh -t xtensa-espressif_esp32s3_zephyr-elf
   ```

3. Espressif HAL のパスと Python ツールを用意する。
   ```sh
   export ESP_IDF_PATH=$PWD/modules/hal/espressif
   pip install esptool>=5.0.2
   ```

## west build の呼び出し例

`ESP_IDF_PATH` を CMake 引数に渡してビルドする。

```sh
west build -b m5stack_cores3/esp32s3/procpu samples/basic/blinky --pristine \
  -- -DESP_IDF_PATH=$PWD/modules/hal/espressif
```

## 既知の問題

上記の手順を踏んでも `xtensa/config/core.h` が見つからないためコンパイルが停止する。Zephyr SDK 0.17.4 同梱の `xtensa-espressif_esp32s3_zephyr-elf` には Xtensa コア設定ヘッダが含まれておらず、Espressif が提供する Xtensa ツールチェーンから `xtensa/config` ツリーを追加で導入する必要がある。

現状のリポジトリには当該ヘッダが含まれていないため、Espressif IDF の `tools/xtensa-esp32s3-elf` などから該当ファイル群を取得するまでビルドは完了しない点に留意すること。
