# quota

Raspberry PiでSTMicroelectronics LSM6DSV16XをI2C接続し、クオータニオンと姿勢角を表示するC++プログラムです。STの`STMems_Standard_C_drivers`をサブモジュールとして利用します。

## 配線

- 3V3 -> センサー VDD / VDDIO
- GND -> センサー GND
- GPIO 2 (SDA) -> SDA
- GPIO 3 (SCL) -> SCL
- センサーのINT1 -> GPIO 27（現在の実装はI2Cポーリングで動作し、GPIO 27は将来の割り込み入力用に確保）
- SA0/SDOをGND -> I2Cアドレス `0x6a`
- SA0/SDOを3V3 -> I2Cアドレス `0x6b`

センサーのI2C信号は3.3 Vにしてください。必要なプルアップ抵抗も確認してください。

## ビルドと実行

```sh
sudo raspi-config  # Interface Options -> I2C を有効化
make
sudo ./quota
```

I2Cアドレスを変更する場合は、バス番号とアドレスを指定できます。

```sh
sudo ./quota 1 0x6b
```

表示は`Q w x y z | deg roll pitch yaw`の順です。加速度とジャイロだけでは絶対方位を測れないため、Yawは時間とともにドリフトします。GPIO 27のINT1を使わず、データレディレジスタをポーリングしています。