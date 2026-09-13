# quota 設計書

## 1. 目的

Raspberry Piに接続したSTMicroelectronics製LSM6DSV16Xから加速度と角速度を取得し、姿勢をクオータニオンおよびDegree単位のEuler角として表示する。

実行プログラム名は`quota`とする。通信方式はRaspberry PiのLinux I2Cデバイスを使用する。

## 2. 対象ハードウェア

- Raspberry Pi（64 bit Raspberry Pi OSを想定）
- STMicroelectronics LSM6DSV16X
- I2C信号: GPIO 2（SDA、物理ピン3）、GPIO 3（SCL、物理ピン5）
- センサーINT1: GPIO 27（物理ピン13）
- 電源: 3.3 V、GND

LSM6DSV16XのSA0/SDO設定によりI2Cアドレスは次のいずれかになる。

| SA0/SDO | 7 bit I2Cアドレス | quota引数 |
|---|---:|---:|
| GND | `0x6a` | 省略時の既定値 |
| 3.3 V | `0x6b` | `sudo ./quota 1 0x6b` |

I2C信号は必ず3.3 V系とする。センサーモジュールにプルアップ抵抗がない場合は、SDA/SCLに適切なI2Cプルアップを用意する。

## 3. ソフトウェア構成

```text
quota.cpp                                  Raspberry Pi側C++アプリケーション
Makefile                                   ビルド定義
stm_driver/lsm6dsv16x_STdC/driver/
  lsm6dsv16x_reg.h                         ST公式ドライバのヘッダ
  lsm6dsv16x_reg.c                         ST公式ドライバの実装
quota                                      ビルドで生成される実行ファイル
```

ST公式ドライバの取得元:

- https://github.com/STMicroelectronics/STMems_Standard_C_drivers
- https://github.com/STMicroelectronics/STMems_Standard_C_drivers/tree/master/lsm6dsv16x_STdC

ドライバはセンサー固有のレジスタ操作を担当し、Raspberry Pi固有のI2C read/write処理は`quota.cpp`が提供する。

## 4. 動作仕様

### 4.1 初期化

1. `/dev/i2c-1`を開く。
2. コマンドラインで指定されたI2Cアドレスを選択する。既定値は`0x6a`。
3. `WHO_AM_I`を読み、LSM6DSV16XのデバイスIDを確認する。
4. ソフトウェアリセットを実行する。
5. Block Data Updateを有効化する。
6. 加速度とジャイロを次の設定にする。

| 項目 | 設定 |
|---|---|
| 加速度 ODR | 120 Hz |
| ジャイロ ODR | 120 Hz |
| 加速度フルスケール | +/-2 g |
| ジャイロフルスケール | +/-2000 dps |

### 4.2 データ取得

データレディレジスタを2 ms間隔でポーリングし、加速度とジャイロの両方が新しくなったときに読み出す。センサーのサンプリング周期は約8.33 ms（120 Hz）である。

I2Cクロック周波数は`quota`内部では設定しない。LinuxおよびRaspberry Piの設定値に従う。400 kHzを使用する場合は`/boot/firmware/config.txt`の共通部分に次を追加する。

```ini
dtparam=i2c_arm_baudrate=400000
```

変更後は再起動する。I2Cクロック400 kHzは通信速度であり、センサー取得周期120 Hzとは別の設定である。

### 4.3 姿勢計算

1. ジャイロのmdps値をrad/sへ変換する。
2. 前回サンプルからの経過時間を使い、角速度をクオータニオンへ積分する。
3. クオータニオンを正規化する。
4. 加速度の大きさが0.5 gから1.5 gの範囲にあるとき、重力方向との誤差で姿勢を補正する。
5. クオータニオンをRoll、Pitch、Yawへ変換してDegreeで表示する。

加速度補正によりRoll/Pitchの長時間ドリフトを抑制する。LSM6DSV16Xには磁気センサーがないため、絶対方位を観測できず、Yawはジャイロバイアスによりドリフトする。

### 4.4 GPIO 27

センサーのINT1をGPIO 27へ配線する設計とする。ただし現行実装はINT1のイベント待ちを使用せず、データレディレジスタをI2Cポーリングしている。したがって、GPIO 27が未接続でも現行プログラムのI2C取得自体は動作する。

## 5. コンパイルに必要なファイルとパッケージ

### 5.1 プロジェクトファイル

- `quota.cpp`
- `Makefile`
- `stm_driver/lsm6dsv16x_STdC/driver/lsm6dsv16x_reg.c`
- `stm_driver/lsm6dsv16x_STdC/driver/lsm6dsv16x_reg.h`

### 5.2 Debian/Raspberry Pi OSパッケージ

```sh
sudo apt update
sudo apt install -y build-essential git i2c-tools
```

必要なものはGCC/G++、GNU Make、Linux I2Cデバイスヘッダ、Git、I2C確認ツールである。

## 6. インストール方法

### 6.1 I2Cの有効化

```sh
sudo raspi-config
```

`Interface Options` -> `I2C` -> `Enable`を選択し、再起動する。

現在のRaspberry Pi OSで`/boot/firmware/config.txt`を直接編集する場合は、次を確認する。

```ini
dtparam=i2c_arm=on
```

400 kHzが必要な場合は、次も追加する。

```ini
dtparam=i2c_arm_baudrate=400000
```

```sh
sudo reboot
```

### 6.2 ソース取得

既存のプロジェクトディレクトリを使用する場合は、`stm_driver`が存在することを確認する。新規に取得する場合は次のようにする。

```sh
cd /home/pi/test
git clone --recursive https://github.com/STMicroelectronics/STMems_Standard_C_drivers.git stm_driver
```

`stm_driver/lsm6dsv16x_STdC/driver/lsm6dsv16x_reg.c`と`.h`が存在することを確認する。

### 6.3 コンパイル

```sh
cd /home/pi/test
make clean
make
```

成功すると、同じディレクトリに実行ファイル`quota`が生成される。

## 7. エラー処理

| 症状 | 主な原因 | 確認方法 |
|---|---|---|
| `/dev/i2c-1: No such file or directory` | I2C未有効化、またはバス番号違い | `ls /dev/i2c-*` |
| `LSM6DSV16X not found` | アドレス違い、配線不良、電源不良 | `sudo i2cdetect -y 1` |
| Permission denied | I2Cアクセス権不足 | `sudo ./quota`、またはユーザーを`i2c`グループへ追加 |
| 値が不安定 | 電源、プルアップ、センサー固定、振動 | 配線と電源を確認 |

## 8. 制約事項

- Yawの絶対角は取得できない。
- 加速度補正はセンサーがほぼ静止し、加速度の大きさが約1 gのときに有効である。
- GPIO 27は現行バージョンでは割り込み入力として使用していない。
- I2Cアドレスは7 bit表記で指定する。`0xD5`や`0xD7`はSTドライバ内部の8 bit表記であり、コマンドラインには指定しない。