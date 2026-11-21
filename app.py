import os
from flask import Flask, request, jsonify
import psycopg2

app = Flask(__name__)

# Kendi bilgilerinize göre doldurun.
DB_CONFIG = {
    "host": os.environ.get("DB_HOST", "localhost"),
    "port": int(os.environ.get("DB_PORT", "5432")),
    "dbname": os.environ.get("DB_NAME", "akilli_tarim"),
    "user": os.environ.get("DB_USER", "postgres"),
    "password": os.environ.get("DB_PASS", "VERITABANI_SIFREN"),
}


def get_conn():
    # Her istekte baglanti acip kapatmak kucuk ogrenci projelerinde kafa agritmiyor
    return psycopg2.connect(**DB_CONFIG)


@app.post("/okuma-ekle")
def okuma_ekle():
    # ESP32'den gelen JSON verisini alip sensor_okumalari tablosuna basiyoruz
    data = request.get_json(force=True)
    sensor_id = int(data.get("sensor_id", 1))
    okunan_deger = float(data["okunan_deger"])
    durum = data.get("durum", "Normal")

    with get_conn() as conn:
        with conn.cursor() as cur:
            cur.execute(
                """
                INSERT INTO sensor_okumalari (sensor_id, okunan_deger, durum)
                VALUES (%s, %s, %s)
                RETURNING okuma_id, olcum_zamani;
                """,
                (sensor_id, okunan_deger, durum),
            )
            row = cur.fetchone()

    return jsonify({"status": "ok", "okuma_id": row[0], "zaman": str(row[1])}), 201


@app.post("/sulama-ekle")
def sulama_ekle():
    # Menuden SULA secilip basildiginda sulama_gecmisi tablosuna yeni kayit atiyoruz
    data = request.get_json(force=True)
    vana_id = int(data.get("vana_id", 1))
    su_tuketimi = float(data.get("su_tuketimi_litre", 12.5))
    tetikleme_tipi = data.get("tetikleme_tipi", "Manuel")
    tetikleyen_okuma_id = data.get("tetikleyen_okuma_id")

    with get_conn() as conn:
        with conn.cursor() as cur:
            cur.execute(
                """
                INSERT INTO sulama_gecmisi (
                    vana_id, baslama_zamani, bitis_zamani,
                    su_tuketimi_litre, tetikleme_tipi, tetikleyen_okuma_id
                )
                VALUES (%s, NOW(), NOW(), %s, %s, %s)
                RETURNING sulama_id;
                """,
                (vana_id, su_tuketimi, tetikleme_tipi, tetikleyen_okuma_id),
            )
            sulama_id = cur.fetchone()[0]

    return jsonify({"status": "ok", "sulama_id": sulama_id}), 201


@app.get("/son-kayit")
def son_kayit():
    # Hem son sensor okumasini hem son sulamayi cekip OLED tarafina ozet donduruyoruz
    with get_conn() as conn:
        with conn.cursor() as cur:
            cur.execute(
                """
                SELECT okuma_id, sensor_id, okunan_deger, durum, olcum_zamani
                FROM sensor_okumalari
                ORDER BY okuma_id DESC
                LIMIT 1;
                """
            )
            okuma = cur.fetchone()

            cur.execute(
                """
                SELECT sulama_id, vana_id, su_tuketimi_litre, tetikleme_tipi, baslama_zamani
                FROM sulama_gecmisi
                ORDER BY sulama_id DESC
                LIMIT 1;
                """
            )
            sulama = cur.fetchone()

    return jsonify({"son_okuma": okuma, "son_sulama": sulama}), 200


if __name__ == "__main__":
    # Tum ag arayuzlerinden (ESP32'nin baglanabilmesi icin) 5000 portunda dinle
    app.run(host="0.0.0.0", port=5000, debug=True)
