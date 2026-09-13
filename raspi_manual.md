# quota 取扱説明書

## 1. 概要

`quota`はRaspberry Piに接続したLSM6DSV16Xの姿勢を表示するプログラムである。画面にはクオータニオンと、クオータニオンから変換したRoll、Pitch、Yawの角度を表示する。

## 2. 配線

電源を切った状態で次のように接続する。

| Raspberry Pi | LSM6DSV16X |
|---|---|
| 3.3 V | VDD、VDDIO |
| GND | GND |
| GPIO 2 / SDA | SDA |
| GPIO 3 / SCL | SCL |
| GPIO 27 | INT1 |

SA0/SDOはI2Cアドレスを決める。

- GND: `0x6a`
- 3.3 V: `0x6b`

GPIO 27は現行プログラムでは使用せず、INT1用の予備接続である。現在のデータ取得はI2Cのデータレディレジスタをポーリングする。

## 3. 初回設定

### 3.1 I2Cを有効にする

```sh
sudo raspi-config
```

`Interface Options` -> `I2C`を有効にしてRaspberry Piを再起動する。

設定ファイルを確認する場合:

```sh
grep i2c_arm /boot/firmware/config.txt
```

次の行が表示されればI2Cは有効である。

```ini
dtparam=i2c_arm=on
```

I2Cを400 kHzで動かす場合は、`/boot/firmware/config.txt`に次を追加して再起動する。

```ini
dtparam=i2c_arm_baudrate=400000
```

これは通信速度の設定であり、センサーの取得周期は別設定の120 Hzである。

### 3.2 必要パッケージをインストールする

```sh
sudo apt update
sudo apt install -y build-essential git i2c-tools
```

## 4. ビルド

プロジェクトディレクトリへ移動してビルドする。

```sh
cd /home/pi/test
make
```

実行ファイル`quota`が生成される。

STドライバがない場合は、プロジェクトディレクトリで取得する。

```sh
git clone --recursive https://github.com/STMicroelectronics/STMems_Standard_C_drivers.git stm_driver
make clean
make
```

## 5. 接続確認

I2Cアドレスを確認する。

```sh
sudo i2cdetect -y 1
```

センサーのSA0/SDOがGNDなら`6a`、3.3 Vなら`6b`が表示される。何も表示されない場合は電源、GND、SDA、SCL、プルアップ抵抗、I2Cアドレスを確認する。

## 6. 起動

### アドレスが0x6aの場合

```sh
sudo ./quota
```

### アドレスが0x6bの場合

```sh
sudo ./quota 1 0x6b
```

第1引数はI2Cバス番号、第2引数は7 bit形式のI2Cアドレスである。

## 7. 表示内容

起動後、次の形式で表示される。

```text
Quaternion (w x y z) | Degree (roll pitch yaw)
Q 0.9999 0.0010 -0.0020 0.0030 | deg 0.12 -0.23 0.34
```

各値の意味は次のとおりである。

- `Q`: 姿勢を表すクオータニオン。順序は`w x y z`。
- `roll`: X軸周りの角度。
- `pitch`: Y軸周りの角度。
- `yaw`: Z軸周りの角度。

角度の単位はDegree（度）である。表示は同じ行を更新するため、端末によっては値が動いて見える。

## 8. 停止

キーボードで`Ctrl+C`を押す。

## 9. 注意事項

- センサーを動かした直後は姿勢が安定するまで少し待つ。
- 加速度補正は静止または低加速度の状態で働く。センサーを急に動かすと一時的に角度がずれることがある。
- ジャイロと加速度だけでは地磁気を測れないため、Yawの絶対方位は分からない。Yawは時間とともにドリフトする。
- センサーとRaspberry Piの信号電圧は3.3 Vにする。5 V信号を直接接続しない。
- `WHO_AM_I`エラーが出た場合は、I2Cアドレス指定と配線を確認する。

## 10. よくあるエラー

### `/dev/i2c-1: No such file or directory`

I2Cが有効になっていないか、使用バスが異なる。`sudo raspi-config`でI2Cを有効化し、`ls /dev/i2c-*`でバス番号を確認する。

### `LSM6DSV16X not found`

指定したアドレスにセンサーが応答していない。`sudo i2cdetect -y 1`で`0x6a`または`0x6b`を確認し、必要なら起動引数を変更する。

### 表示が更新されない

データレディが検出されていない可能性がある。SDA/SCLの配線、電源、GND、I2Cプルアップ、センサーの向きを確認する。