# ESP32s3-Akilli-tarim-sistemi
esp32s3 ile veri tabanındaki bilgileri birleştirerek sulama , son kayıt gibi durumları oled ekrana yazdırdım.

# ESP32-S3 Akıllı Tarım ve Sulama Terminali

ESP32-S3 kullanarak rotary encoder ile menüde gezdiğim, SSD1306 OLED ekranda durumları ve API cevaplarını gördüğüm, arka tarafta da Flask ve PostgreSQL ile konuşan bir prototip.

## Ne Yapıyor?
- Rotary encoder'ı çevirince menüde **1-NEM EKLE**, **2-SULA** ve **3-SON KAYIT** arasında geçiş yapıyorum.
- Butona basınca ESP-IDF üzerinden yerel Flask API'ye istek atıyorum:
  - `1-NEM EKLE` -> `/okuma-ekle` (POST) ile `sensor_okumalari` tablosuna kayıt atıyor.
  - `2-SULA` -> `/sulama-ekle` (POST) ile `sulama_gecmisi` tablosuna sulama kaydı düşüyor.
  - `3-SON KAYIT` -> `/son-kayit` (GET) ile son verileri çekip OLED'in alt satırında özet gösteriyor.

## Bağlantılar (ESP32-S3)
- **OLED SDA:** GPIO 8
- **OLED SCL:** GPIO 9 (I2C adresi genelde `0x3C`)
- **Rotary CLK:** GPIO 4
- **Rotary DT:** GPIO 5
- **Rotary SW:** GPIO 6

## Nasıl Çalıştırdım?
1. PostgreSQL'de `akilli_tarim` veritabanını ve `sensor_okumalari` / `sulama_gecmisi` tablolarını hazırladım.
2. `app.py` dosyasını çalıştırıp Flask sunucusunu ayağa kaldırdım.
3. `main.c` içindeki Wi-Fi bilgileri ve API adresini kendi ağıma göre ayarlayıp ESP-IDF ile ESP32-S3'e build + flash yaptım.

## Notlar
- Şu an geliştirme/prototip aşamasında; ileride gerçek toprak nem sensörü ve röle/pompa tarafını da bağlamayı düşünüyorum.
- Repoya yüklerken kendi Wi-Fi şifremi ve veritabanı şifremi yer tutucu olarak bıraktım.

https://github.com/user-attachments/assets/a4f42483-4c96-413d-8216-b1c0c5b875f1


