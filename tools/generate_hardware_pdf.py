#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Ekstrem Kosul Ayrık GNSS Terminali - Kapsamlı Devre & PCB Tasarım Rehberi
Profesyonel PDF Üretim Aracı (ReportLab & Segoe UI)
"""

import os
import sys
from reportlab.lib.pagesizes import A4
from reportlab.lib import colors
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.platypus import (
    SimpleDocTemplate, Paragraph, Spacer, Table, TableStyle, PageBreak, KeepTogether, HRFlowable
)
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.pdfgen import canvas

# ---------------------------------------------------------------------------
# Font Kaydı (Türkçe Karakter Tam Uyumu)
# ---------------------------------------------------------------------------
try:
    pdfmetrics.registerFont(TTFont('SegoeUI', 'C:/Windows/Fonts/segoeui.ttf'))
    pdfmetrics.registerFont(TTFont('SegoeUI-Bold', 'C:/Windows/Fonts/segoeuib.ttf'))
    pdfmetrics.registerFont(TTFont('SegoeUI-Italic', 'C:/Windows/Fonts/segoeuii.ttf'))
    pdfmetrics.registerFont(TTFont('SegoeUI-BoldItalic', 'C:/Windows/Fonts/segoeuiz.ttf'))
    FONT_NORMAL = 'SegoeUI'
    FONT_BOLD = 'SegoeUI-Bold'
    FONT_ITALIC = 'SegoeUI-Italic'
    FONT_BOLD_ITALIC = 'SegoeUI-BoldItalic'
except Exception as e:
    print(f"Font yukleme hatasi: {e}, varsayilan Helvetica kullaniliyor.")
    FONT_NORMAL = 'Helvetica'
    FONT_BOLD = 'Helvetica-Bold'
    FONT_ITALIC = 'Helvetica-Oblique'
    FONT_BOLD_ITALIC = 'Helvetica-BoldOblique'

# ---------------------------------------------------------------------------
# Renk Paleti (Taktik Havacılık & Mühendislik Teması)
# ---------------------------------------------------------------------------
NAVY = colors.HexColor("#0B192C")
STEEL = colors.HexColor("#1E3E62")
ORANGE = colors.HexColor("#FF6500")
SLATE_GRAY = colors.HexColor("#334155")
LIGHT_BG = colors.HexColor("#F8FAFC")
BORDER_COLOR = colors.HexColor("#CBD5E1")
TEXT_DARK = colors.HexColor("#0F172A")
ACCENT_GREEN = colors.HexColor("#0D9488")
ACCENT_RED = colors.HexColor("#B91C1C")

# ---------------------------------------------------------------------------
# Sayfa Numaralandırma & Header/Footer Canvas Sınıfı
# ---------------------------------------------------------------------------
class NumberedCanvas(canvas.Canvas):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self._saved_page_states = []

    def showPage(self):
        self._saved_page_states.append(dict(self.__dict__))
        self._startPage()

    def save(self):
        num_pages = len(self._saved_page_states)
        for state in self._saved_page_states:
            self.__dict__.update(state)
            self.draw_header_footer(num_pages)
            super().showPage()
        super().save()

    def draw_header_footer(self, page_count):
        self.saveState()
        self.setFont(FONT_NORMAL, 8)
        self.setFillColor(colors.HexColor("#64748B"))

        # Sayfa 1 (Kapak) haricinde üst ve alt bilgi çiz
        if self._pageNumber > 1:
            # Üst Bilgi (Header)
            self.drawString(54, 800, "EKSTREM KOŞUL AYRIK GNSS TERMİNALİ  |  DONANIM & PCB TASARIM KILAVUZU")
            self.drawRightString(541, 800, "DOKÜMAN: HW-RECIPE-001")
            self.setStrokeColor(BORDER_COLOR)
            self.setLineWidth(0.5)
            self.line(54, 792, 541, 792)

            # Alt Bilgi (Footer)
            self.line(54, 45, 541, 45)
            self.drawString(54, 32, "Mühendislik Referans Tasarımı - KiCad & SMT Üretim Kılavuzu")
            page_str = f"Sayfa {self._pageNumber} / {page_count}"
            self.drawRightString(541, 32, page_str)

        self.restoreState()

def create_guide_pdf(output_path):
    doc = SimpleDocTemplate(
        output_path,
        pagesize=A4,
        leftMargin=54,
        rightMargin=54,
        topMargin=54,
        bottomMargin=54
    )

    styles = getSampleStyleSheet()

    # Özel Stiller
    title_style = ParagraphStyle(
        'DocTitle',
        parent=styles['Normal'],
        fontName=FONT_BOLD,
        fontSize=24,
        leading=28,
        textColor=NAVY,
        spaceAfter=8
    )

    subtitle_style = ParagraphStyle(
        'DocSubtitle',
        parent=styles['Normal'],
        fontName=FONT_NORMAL,
        fontSize=12,
        leading=16,
        textColor=ORANGE,
        spaceAfter=15
    )

    h1_style = ParagraphStyle(
        'Heading1_Custom',
        parent=styles['Normal'],
        fontName=FONT_BOLD,
        fontSize=15,
        leading=19,
        textColor=NAVY,
        spaceBefore=16,
        spaceAfter=8,
        keepWithNext=True
    )

    h2_style = ParagraphStyle(
        'Heading2_Custom',
        parent=styles['Normal'],
        fontName=FONT_BOLD,
        fontSize=11.5,
        leading=15,
        textColor=STEEL,
        spaceBefore=11,
        spaceAfter=5,
        keepWithNext=True
    )

    body_style = ParagraphStyle(
        'Body_Custom',
        parent=styles['Normal'],
        fontName=FONT_NORMAL,
        fontSize=9.2,
        leading=13.5,
        textColor=TEXT_DARK,
        spaceAfter=6
    )

    bullet_style = ParagraphStyle(
        'Bullet_Custom',
        parent=styles['Normal'],
        fontName=FONT_NORMAL,
        fontSize=9,
        leading=13,
        textColor=TEXT_DARK,
        leftIndent=14,
        spaceAfter=4
    )

    callout_style = ParagraphStyle(
        'Callout_Custom',
        parent=styles['Normal'],
        fontName=FONT_NORMAL,
        fontSize=8.8,
        leading=12.5,
        textColor=NAVY
    )

    code_block_style = ParagraphStyle(
        'CodeBlock_Custom',
        parent=styles['Normal'],
        fontName=FONT_NORMAL,
        fontSize=8,
        leading=11,
        textColor=colors.HexColor("#0F172A")
    )

    table_header_style = ParagraphStyle(
        'TableHeader',
        parent=styles['Normal'],
        fontName=FONT_BOLD,
        fontSize=8.5,
        leading=11,
        textColor=colors.white,
        alignment=0
    )

    table_cell_style = ParagraphStyle(
        'TableCell',
        parent=styles['Normal'],
        fontName=FONT_NORMAL,
        fontSize=8,
        leading=11,
        textColor=TEXT_DARK
    )

    table_cell_bold = ParagraphStyle(
        'TableCellBold',
        parent=styles['Normal'],
        fontName=FONT_BOLD,
        fontSize=8,
        leading=11,
        textColor=NAVY
    )

    story = []

    # =========================================================================
    # KAPAK VE BAŞLIK BÖLÜMÜ
    # =========================================================================
    story.append(Paragraph("EKSTREM KOŞUL AYRIK GNSS TERMİNALİ", title_style))
    story.append(Paragraph("Donanım Mimarisi, Çanta Kartı Detayları ve Sıfırdan KiCad ile PCB Tasarım Kılavuzu", subtitle_style))
    story.append(HRFlowable(width="100%", thickness=2, color=ORANGE, spaceBefore=0, spaceAfter=12))

    # Meta Bilgi Tablosu
    meta_data = [
        [
            Paragraph("<b>Doküman No:</b> HW-RECIPE-001", body_style),
            Paragraph("<b>Revizyon:</b> 1.0 (Nihai)", body_style),
            Paragraph("<b>Tarih:</b> Ekim 2026", body_style)
        ],
        [
            Paragraph("<b>Hedef:</b> 2 Podlu Ayrık PCB Mimarisi", body_style),
            Paragraph("<b>Ana İşlemci:</b> STM32U575 (Cortex-M33)", body_style),
            Paragraph("<b>EDA Aracı:</b> KiCad 8 / 9", body_style)
        ]
    ]
    t_meta = Table(meta_data, colWidths=[160, 160, 167])
    t_meta.setStyle(TableStyle([
        ('BACKGROUND', (0,0), (-1,-1), LIGHT_BG),
        ('BOX', (0,0), (-1,-1), 0.5, BORDER_COLOR),
        ('INNERGRID', (0,0), (-1,-1), 0.5, BORDER_COLOR),
        ('TOPPADDING', (0,0), (-1,-1), 4),
        ('BOTTOMPADDING', (0,0), (-1,-1), 4),
    ]))
    story.append(t_meta)
    story.append(Spacer(1, 14))

    # Giriş Özeti
    story.append(Paragraph(
        "Bu kılavuz, <b>daha önce hiç baskı devre (PCB) çizmemiş</b> bir mühendisin dahi sıfırdan "
        "başlayarak askeri ve ekstrem dağcılık standartlarında (+40°C ile -30°C arası) çalışan, parazit "
        "üretmeyen, RF sağırlığına yol açmayan ve donmayan iki adet profesyonel kartı tasarlayıp fabrikada (JLCPCB vb.) "
        "ürettirebilmesi için adım adım bir rehber olarak hazırlanmıştır.", body_style
    ))
    story.append(Spacer(1, 10))

    # =========================================================================
    # 1. BÖLÜM: NEDEN AYRIK GÖVDE (SPLIT-NODE)? FİZİKSEL GERÇEKLER
    # =========================================================================
    story.append(Paragraph("1. Temel Mimari: Neden Tek Değil de İki Ayrı Kart (Ayrık Pod)?", h1_style))
    story.append(Paragraph(
        "Tüketici elektroniğinde (akıllı saatler, el GPS'leri) her şey tek bir gövdeye tıkılır. Ancak zorlu dağcılık "
        "ve kanyon koşullarında bu yaklaşım iki ölümcül fiziksel duvara çarpar:", body_style
    ))
    story.append(Paragraph(
        "<b>1. İnsan Vücudunun RF Yutması (SAR Etkisi):</b> İnsan dokusu tuzlu su ve kandan oluşur. "
        "1.2–1.6 GHz GNSS (GPS) ve 868 MHz LoRa radyo dalgalarını <b>15 ila 25 dB zayıflatır</b>. Göğse veya kola "
        "takılan bir anten gökyüzünün %50'sini görmez, uydular kilitlenemez. Anten mutlaka sırtta, baş hizasının üstünde "
        "(zenit açısında) olmalıdır.", bullet_style
    ))
    story.append(Paragraph(
        "<b>2. -20°C'de Lityum Bataryanın Çökmesi (ESR Sag):</b> Soğukta lityum pilin iç direnci (ESR) 15-20 kat fırlar. "
        "LoRa veya uydu telsizi yayına girdiği an pil gerilimi anında 0.5V çöker ve cihaz kapanır. Bu sebeple güç devresi, "
        "özel süperkapasitör tamponu ve ağır işlemci çantanın korumalı gövdesinde kalmalıdır.", bullet_style
    ))
    story.append(Spacer(1, 6))

    # Mimari Kutu
    callout_data = [[
        Paragraph(
            "<b>Sistem İki Fiziksel Karttan Oluşur:</b><br/>"
            "• <b>Göğüs HMI Podu:</b> Ekran, 4 eldiven butonu, barometre, IMU sensörü ve yardımcı düşük güç MCU.<br/>"
            "• <b>Çanta RF & Güç Podu:</b> Ana Cortex-M33 MCU, çift frekanslı GNSS, LoRa, Iridium uydu modemi, "
            "süperkapasitörler ve iki kademeli ultra-temiz güç santrali.<br/>"
            "• <b>Bağlantı:</b> Aralarında 1.2 metrelik 4-damarlı endüstriyel kablo: <b>[12V Güç, GND, CAN_H, CAN_L]</b>. "
            "<i>(Asla kablo üzerinden hızlı SPI geçirilmez! Kablo parazit antenine dönüp uyduları kör eder.)</i>",
            callout_style
        )
    ]]
    t_callout = Table(callout_data, colWidths=[487])
    t_callout.setStyle(TableStyle([
        ('BACKGROUND', (0,0), (-1,-1), colors.HexColor("#EFF6FF")),
        ('BOX', (0,0), (-1,-1), 1, colors.HexColor("#3B82F6")),
        ('TOPPADDING', (0,0), (-1,-1), 7),
        ('BOTTOMPADDING', (0,0), (-1,-1), 7),
    ]))
    story.append(t_callout)
    story.append(Spacer(1, 14))

    # =========================================================================
    # 2. BÖLÜM: SIFIRDAN PCB VE KICAD MANTIĞI (BAŞLANGIÇ REHBERİ)
    # =========================================================================
    story.append(Paragraph("2. Sıfırdan Başlayanlar İçin PCB ve KiCad Mantığı", h1_style))
    story.append(Paragraph(
        "Daha önce kart çizmediysen gözün korkmasın; bir PCB tasarımı iki ana aşamadan oluşur: "
        "<b>Şematik (Mantıksal Bağlantı)</b> ve <b>PCB Yerleşimi (Fiziksel Çizim)</b>.", body_style
    ))

    kicad_steps = [
        [
            Paragraph("<b>Aşama</b>", table_header_style),
            Paragraph("<b>Ne Yapılır? (İşlem Mantığı)</b>", table_header_style),
            Paragraph("<b>KiCad Karşılığı</b>", table_header_style)
        ],
        [
            Paragraph("<b>1. Şematik Çizimi</b>", table_cell_bold),
            Paragraph("Hangi parçanın hangi bacağı nereye bağlanacak? Dirençler, çipler sembol olarak seçilir, çizgilerle birleştirilir.", table_cell_style),
            Paragraph("<i>Schematic Editor</i> (.kicad_sch)", table_cell_style)
        ],
        [
            Paragraph("<b>2. Kural Kontrolü (ERC)</b>", table_cell_bold),
            Paragraph("Bağlanmamış açıkta bacak var mı, kısa devre var mı diye kontrol edilir.", table_cell_style),
            Paragraph("<i>Electrical Rules Check</i>", table_cell_style)
        ],
        [
            Paragraph("<b>3. Kılıf Eşleştirme</b>", table_cell_bold),
            Paragraph("Sembollerin gerçek hayattaki fiziksel boyutları seçilir (Örn: 0603 SMD direnç, QFN-48 çip).", table_cell_style),
            Paragraph("<i>Assign Footprints</i>", table_cell_style)
        ],
        [
            Paragraph("<b>4. PCB'ye Aktarma</b>", table_cell_bold),
            Paragraph("Şematikteki tüm elemanlar tek tuşla PCB ekranına dökülür.", table_cell_style),
            Paragraph("Kısayol: <b>F8</b> tuşu", table_cell_style)
        ],
        [
            Paragraph("<b>5. Yerleşim & Hat Çekme</b>", table_cell_bold),
            Paragraph("Parçalar mantıklı dizilir, aralarındaki sanal çizgiler (ratsnest) gerçek bakır yollara dönüştürülür.", table_cell_style),
            Paragraph("<i>PCB Editor</i> (.kicad_pcb)", table_cell_style)
        ],
        [
            Paragraph("<b>6. Hata Denetimi (DRC)</b>", table_cell_bold),
            Paragraph("Yollar birbirine fazla yakın mı, fabrika sınırlarını aştık mı denetlenir.", table_cell_style),
            Paragraph("<i>Design Rules Check</i>", table_cell_style)
        ],
        [
            Paragraph("<b>7. Üretim Çıktısı</b>", table_cell_bold),
            Paragraph("Fabrikanın lazer kazıma ve dizgi makinelerinin anlayacağı dosyalar üretilir.", table_cell_style),
            Paragraph("Gerber, BOM ve CPL (Pick & Place)", table_cell_style)
        ]
    ]
    t_kicad = Table(kicad_steps, colWidths=[100, 240, 147])
    t_kicad.setStyle(TableStyle([
        ('BACKGROUND', (0,0), (-1,0), NAVY),
        ('GRID', (0,0), (-1,-1), 0.5, BORDER_COLOR),
        ('ROWBACKGROUNDS', (0,1), (-1,-1), [colors.white, LIGHT_BG]),
        ('TOPPADDING', (0,0), (-1,-1), 4),
        ('BOTTOMPADDING', (0,0), (-1,-1), 4),
    ]))
    story.append(t_kicad)
    story.append(Spacer(1, 14))

    # 4 Katman Kuralı
    story.append(Paragraph("Neden 2 Katman Değil de 4 Katmanlı PCB?", h2_style))
    story.append(Paragraph(
        "Standart 2 katmanlı kartlarda bir yüzden sinyal, diğer yüzden toprak geçirilir. Ancak yüksek frekanslı "
        "RF ve 5 Mbps CAN sinyalleri toprağı böler, parazit yayar. <b>4 Katmanlı Standart JLC2313 Stackup</b> kullanıyoruz:", body_style
    ))
    story.append(Paragraph("• <b>Katman 1 (Top - Üst Yüzey):</b> Tüm çipler, 50 Ohm RF anten yolları ve diferansiyel CAN hatları.", bullet_style))
    story.append(Paragraph("• <b>Katman 2 (Inner 1 - Deliksiz Toprak/GND):</b> Yüzde 100 kesintisiz bakır toprak düzlemi. RF'in kalkanıdır.", bullet_style))
    story.append(Paragraph("• <b>Katman 3 (Inner 2 - Güç Düzlemleri):</b> 3.3V Dijital ve 3.0V RF güç besleme bölgeleri.", bullet_style))
    story.append(Paragraph("• <b>Katman 4 (Bottom - Alt Yüzey):</b> Kalan yollar, konnektör lehimleri ve pasif elemanlar.", bullet_style))

    story.append(PageBreak())

    # =========================================================================
    # 3. BÖLÜM: ÇANTA KARTI (BACKPACK RF & COMPUTING NODE) DETAYLI ANLATIM
    # =========================================================================
    story.append(Paragraph("3. Çanta Kartı (Backpack RF & Computing Node) Detaylı Tasarımı", h1_style))
    story.append(Paragraph(
        "Çanta kartı, sistemin tüm karmaşık işlerini üstlenen kalbidir. Bu kart üzerinde yer alan "
        "<b>7 ana devre bloğunu</b> ve çalışma prensiplerini inceleyelim:", body_style
    ))
    story.append(Spacer(1, 4))

    # BLOK 1
    story.append(Paragraph("Blok 1: Ana İşlemci Katı (STM32U575CIT6)", h2_style))
    story.append(Paragraph(
        "<b>Neden Bu Çip?</b> ARM Cortex-M33 çekirdeği 160 MHz'de çalışır, donanımsal kayan nokta birimi (FPU) vardır. "
        "En önemlisi <b>2 MB dahili çift-bank Flash belleğe</b> sahiptir. Geliştirdiğimiz sıkıştırılmış CGPX ve "
        "50 metrelik dağ yükseklik (DEM) haritaları bu belleğe gömülür. Harici yavaş SPI Flash çiplerine ihtiyaç duyulmaz; "
        "bu da sistemin sıfır gecikmeyle ve kilitlenmeden çalışmasını sağlar.", body_style
    ))
    story.append(Paragraph(
        "• <b>Kristal Devresi:</b> 16 MHz ana osilatör kristali (10 ppm tolerans + 2 adet 12 pF kondansatör) ve 32.768 kHz RTC kristali.<br/>"
        "• <b>Dekuplaj (Decoupling):</b> Çipin etrafındaki her bir VDD bacağına 1 adet 100 nF (0402 veya 0603) seramik kondansatör "
        "tam bacağın dibine (maksimum 1.5 mm mesafeye) yerleştirilir.<br/>"
        "• <b>Programlama Header'ı:</b> 4-pin SWD soketi: [3.3V, SWDIO, SWCLK, GND]. ST-Link programlayıcı ile 2 saniyede kod atılır.",
        bullet_style
    ))
    story.append(Spacer(1, 6))

    # BLOK 2
    story.append(Paragraph("Blok 2: Çift Frekanslı GNSS Alıcı Katı (Quectel LC29H / TBS M10Q)", h2_style))
    story.append(Paragraph(
        "<b>Kanyonlarda Hayat Kurtaran L1/L5 Farkı:</b> Tek frekanslı standart GPS (L1: 1575 MHz), dik kaya duvarlarından "
        "yansıyan uyduları doğrudan gelen uydulardan ayıramaz; bu duruma <i>Multipath (Çok Yollu Sahte Sinyal)</i> denir. "
        "LC29H çift bantlıdır (L1 + L5: 1176 MHz). Farklı frekanslar kayalıktan yansırken faz farkı oluşturur; alıcı "
        "yansıyan sahte sinyalleri donanımsal olarak çöpe atar.", body_style
    ))
    story.append(Paragraph(
        "• <b>Anten Hattı:</b> Kart üzerinde U.FL konnektör. U.FL'den modül girişine giden bakır yol <b>tam 50 Ohm kontrollü empedans</b> "
        "ile çizilir ve her iki yanına 1 mm aralıklarla GND delikleri (Via Stitching) dizilir.<br/>"
        "• <b>LNA Beslemesi (Bias-Tee):</b> Aktif antenin içindeki dahili yükselteci beslemek için RF hattına 33 nH indüktör üzerinden "
        "temiz 3.0V DC voltaj enjekte edilir.<br/>"
        "• <b>Haberleşme:</b> MCU ile UART hattı üzerinden (115200 veya 921600 baud). Yazdığımız NMEA dairesel DMA motoru sıfır CPU yüküyle veriyi çeker.",
        bullet_style
    ))
    story.append(Spacer(1, 6))

    # BLOK 3
    story.append(Paragraph("Blok 3: Manga İçi LoRa Mesh Telsiz Katı (Semtech SX1262)", h2_style))
    story.append(Paragraph(
        "<b>Pratik Üretim İpucu (Ebyte E22-900M22S):</b> SX1262 entegresini çip seviyesinde lehimlemek minik bobinler ve kristaller "
        "gerektirdiği için zordur. Bunun yerine piyasada çok ucuz olan, içerisinde SX1262, TCXO sıcaklık kompanzasyonlu kristal ve "
        "RF filtreleri hazır metal kalkan içinde gelen <b>Ebyte E22-900M22S SMD modülü</b> kartın üzerine doğrudan lehimlenir.", body_style
    ))
    story.append(Paragraph(
        "• <b>Güç & Frekans:</b> 868.1 MHz ISM bandı, +22 dBm (160 mW) çıkış gücü.<br/>"
        "• <b>Bağlantı:</b> MCU ile SPI veriyolu (SCK, MISO, MOSI, NSS) + Kesme pinleri (DIO1, BUSY, NRST).<br/>"
        "• <b>Hava Paketi:</b> Geliştirdiğimiz 16-baytlık <code>squad_beacon_packet_t</code> sayesinde havada kalma süresi sadece 41 ms'dir. "
        "Bataryayı tüketmez ve düşman telsiz dinleyicileri (EW) tarafından yakalanamaz.",
        bullet_style
    ))
    story.append(Spacer(1, 6))

    # BLOK 4
    story.append(Paragraph("Blok 4: RockBLOCK 9603 Iridium Uydu Acil Durum Soketi", h2_style))
    story.append(Paragraph(
        "Dağ başında LoRa menzili bittiğinde veya dağcı buzul yarığına düştüğünde uydu şebekesine bağlanılır.", body_style
    ))
    story.append(Paragraph(
        "• <b>Sıfır Kaçak Akım Anahtarı (P-MOSFET):</b> Iridium modemi boştayken bile ciddi akım çeker. Modemin güç bacağına "
        "yüksek akımlı P-MOSFET (<code>Si2301CDS</code>) konur. Cihaz normal seyrindeyken MCU bu MOSFET'i tamamen kapatır; kaçak akım <b>0.0 µA</b> olur.<br/>"
        "• <b>1.5A Pik Darbe Beslemesi:</b> Uyduya paket basarken modem 1.5 Amper anlık darbe akımı çeker. Bu akım pilden değil, "
        "aşağıda anlatılan kart üstü süperkapasitör tamponundan emilir.",
        bullet_style
    ))

    story.append(Spacer(1, 6))

    # BLOK 5
    story.append(Paragraph("Blok 5: Sub-Zero Güç Katı & İki Kademeli Regülatör (En Kritik Mühendislik)", h2_style))
    story.append(Paragraph(
        "Burası bir tasarımcının yapabileceği en büyük hataların engellendiği yerdir:", body_style
    ))

    power_table_data = [
        [
            Paragraph("<b>Kademe</b>", table_header_style),
            Paragraph("<b>Kullanılan Parça</b>", table_header_style),
            Paragraph("<b>Mühendislik Görevi ve Önemi</b>", table_header_style)
        ],
        [
            Paragraph("<b>Süperkapasitör Tamponu</b>", table_cell_bold),
            Paragraph("2x 5.0F / 2.7V seri bağlı (2.5F / 5.4V)", table_cell_style),
            Paragraph("-20°C'de bataryanın iç direnci (ESR) tavan yaptığında RF modülleri yayına girince voltajın çökmesini (voltage sag) ve MCU'nun resetlenmesini engeller.", table_cell_style)
        ],
        [
            Paragraph("<b>Kademe 1: Buck-Boost</b>", table_cell_bold),
            Paragraph("TI TPS63020DSJR (%96 Verim)", table_cell_style),
            Paragraph("Bataryanın 3.0V ile 4.2V arasındaki değişken voltajını sabit 3.3V VDD_DIG (dijital ray) seviyesine regüle eder (MCU, CAN ve lojik için).", table_cell_style)
        ],
        [
            Paragraph("<b>Pi-Filtre İzolasyonu</b>", table_cell_bold),
            Paragraph("Murata BLM18HE152SN1D + 2x 10uF", table_cell_style),
            Paragraph("Buck regülatörün ürettiği 2.4 MHz anahtarlama gürültüsünü 18.2 dB bastırarak RF LDO girişini temizler.", table_cell_style)
        ],
        [
            Paragraph("<b>Kademe 2: Ultra-High PSRR LDO</b>", table_cell_bold),
            Paragraph("TI TPS7A2030 (95dB PSRR)", table_cell_style),
            Paragraph("GNSS ve LoRa LNA alıcıları için 3.0V ultra-sessiz VDD_RF üretir. Kalan parazit 7 uV'nin altına iner. Uydularda sıfır sağırlık (0 dB desense).", table_cell_style)
        ]
    ]
    t_power = Table(power_table_data, colWidths=[105, 125, 257])
    t_power.setStyle(TableStyle([
        ('BACKGROUND', (0,0), (-1,0), STEEL),
        ('GRID', (0,0), (-1,-1), 0.5, BORDER_COLOR),
        ('ROWBACKGROUNDS', (0,1), (-1,-1), [colors.white, LIGHT_BG]),
        ('TOPPADDING', (0,0), (-1,-1), 4),
        ('BOTTOMPADDING', (0,0), (-1,-1), 4),
    ]))
    story.append(t_power)
    story.append(Spacer(1, 8))

    # BLOK 6 ve 7
    story.append(Paragraph("Blok 6: Harici Nano-Power Bekçi (TI TPL5010)", h2_style))
    story.append(Paragraph(
        "Aşırı soğukta MCU'nun iç saati donsa veya yazılım kilitlense bile cihaz dağ başında açık kalamaz. "
        "Sadece <b>35 nA</b> akım çeken bu bağımsız çip, her 30 saniyede bir MCU'dan sinyal bekler. "
        "Sinyal gelmezse tüm kartın elektriğini kesip soğuk donanımsal reset (Power-Cycle) atar.", body_style
    ))

    story.append(Paragraph("Blok 7: CAN-FD Göğüs Podu Çıkışı (TI TCAN337G & M8 Soket)", h2_style))
    story.append(Paragraph(
        "Çanta ile göğüs arasındaki 1.2 metrelik kablonun iki ucunda birer adet <b>120 Ohm sonlandırma direnci</b>, "
        "ortak mod şok bobini (<code>TDK ACM2012</code>) ve 30 kV elektrostatik şok koruma diyotu (<code>PESD2CAN</code>) yer alır. "
        "Kablo dışarıdan ezilse veya darbe alsa bile diferansiyel sinyal sayesinde tek bir bit dahi bozulmaz.", body_style
    ))

    story.append(PageBreak())

    # =========================================================================
    # 4. BÖLÜM: GÖĞÜS HMI PODU ÖZETİ
    # =========================================================================
    story.append(Paragraph("4. Göğüs HMI Podu (Chest HMI Pod) Tasarımı", h1_style))
    story.append(Paragraph(
        "Göğüs kartı kullanıcının doğrudan gözü ve elidir. Çok hafif, ince ve düşük güçlü olmak zorundadır:", body_style
    ))

    chest_components = [
        [
            Paragraph("<b>Bileşen</b>", table_header_style),
            Paragraph("<b>Parça Kodu</b>", table_header_style),
            Paragraph("<b>Fonksiyon ve Tasarım İpucu</b>", table_header_style)
        ],
        [
            Paragraph("<b>Companion MCU</b>", table_cell_bold),
            Paragraph("STM32G0B1CEU6 (QFP-48)", table_cell_style),
            Paragraph("Çok ucuz, ultra düşük güç tüketimi, dahili FDCAN kontrolcüsü var. Ekranı ve butonları yönetir.", table_cell_style)
        ],
        [
            Paragraph("<b>MIP Ekran</b>", table_cell_bold),
            Paragraph("Sharp LS027B7DH01A (2.7\")", table_cell_style),
            Paragraph("400x240 Memory LCD. Sıvı kristali donmaz, güneş altında ayna gibi okunur, 50 uW çeker. 10-pin FPC şerit soketle bağlanır.", table_cell_style)
        ],
        [
            Paragraph("<b>Taktik Tuşlar</b>", table_cell_bold),
            Paragraph("4x C&K KSC Serisi Buton", table_cell_style),
            Paragraph("Eldivenle basılabilen, 3.5N sert basma hisli butonlar. Donanımsal RC filtre ile ark gürültüsü (debounce) önlenir.", table_cell_style)
        ],
        [
            Paragraph("<b>Barometre</b>", table_cell_bold),
            Paragraph("Bosch BMP390 (LGA-10)", table_cell_style),
            Paragraph("±0.03 hPa hassasiyet (~25 cm dikey çözünürlük). Buzul yarığı düşüşünü irtifa kaybından anında yakalar.", table_cell_style)
        ],
        [
            Paragraph("<b>IMU İvmeölçer</b>", table_cell_bold),
            Paragraph("ST LSM6DSOX (LGA-14)", table_cell_style),
            Paragraph("6-eksen sensör. Serbest düşüş (|a| < 0.35g) ve zemin darbe şokunu (> 4.5g) tespit eder.", table_cell_style)
        ],
        [
            Paragraph("<b>CAN Arayüzü</b>", table_cell_bold),
            Paragraph("TI TCAN337G (SOT-23-8)", table_cell_style),
            Paragraph("Çanta kartından gelen CAN-FD verisini alır ve ekran komutlarına dönüştürür.", table_cell_style)
        ]
    ]
    t_chest = Table(chest_components, colWidths=[95, 135, 257])
    t_chest.setStyle(TableStyle([
        ('BACKGROUND', (0,0), (-1,0), NAVY),
        ('GRID', (0,0), (-1,-1), 0.5, BORDER_COLOR),
        ('ROWBACKGROUNDS', (0,1), (-1,-1), [colors.white, LIGHT_BG]),
        ('TOPPADDING', (0,0), (-1,-1), 4),
        ('BOTTOMPADDING', (0,0), (-1,-1), 4),
    ]))
    story.append(t_chest)
    story.append(Spacer(1, 14))

    # =========================================================================
    # 5. BÖLÜM: PCB ÇİZERKEN ASLA YAPILMAYACAK 10 ALTIN KURAL
    # =========================================================================
    story.append(Paragraph("5. PCB Çizerken Asla Çiğnenmeyecek 10 Altın Kural", h1_style))
    story.append(Paragraph(
        "İlk kez devre çizenlerin kartlarının çalışmamasına neden olan en yaygın hatalar ve çözümleri:", body_style
    ))

    rules = [
        "<b>1. Dekuplaj Kapasitörünü Uzağa Koyma:</b> 100 nF kondansatör çipin bacağına ne kadar yakınsa o kadar etkilidir. 3 mm'den uzağa koyarsan yolun parazitik endüktansı kondansatörü etkisiz kılar.",
        "<b>2. RF Yolunun Altındaki Toprağı Bölme:</b> Anten yolunun tam altındaki 2. Katman (GND) düzleminde ASLA delik veya başka bir sinyal yolu geçmemelidir. RF dönüş akımı kesilirse kart verici antene döner.",
        "<b>3. 50 Ohm Microstrip Çizgisini Göz Kararı Çizme:</b> KiCad'in içindeki 'PCB Calculator' aracını aç. 4 katmanlı JLC2313 kart için 0.20 mm genişlikte çizgi çek ve her iki yanına GND dök.",
        "<b>4. Anahtarlamalı Güç Kaynağının (Buck) Bobinini Ortaya Koyma:</b> TPS63020 ve 2.2 uH bobinini kartın en kenarına koy. Altından hiçbir veri yolu geçirme.",
        "<b>5. Kristal Yollarını Uzun Tutma:</b> 16 MHz kristal MCU'nun ilgili bacaklarının hemen dibinde olmalı ve etrafı toprak koruma halkasıyla (guard ring) sarılmalıdır.",
        "<b>6. CAN_H ve CAN_L Yollarını Ayrı Gezdirme:</b> Diferansiyel hatlar birbirine paralel ve eşit uzunlukta çekilmelidir (Diferansiyel çift - Differential Pair Routing).",
        "<b>7. Termal Rahatlatmayı (Thermal Relief) Unutma:</b> Toprak düzlemine bağlanan bacaklarda lehimleme zorlaşmasın diye haç şeklinde termal rahatlatma bağlantıları bırak.",
        "<b>8. Test Noktaları (Test Points) Bırak:</b> 3.3V, 3.0V, GND, SWDIO, SWCLK ve CAN hatlarına multimetre probunun değebileceği küçük 1 mm'lik dairesel bakır test padleri koy.",
        "<b>9. İpek Baskıyı (Silkscreen) Lehim Padlerinin Üstüne Taşırma:</b> Fabrika yazıyı padin üstüne basarsa lehim tutmaz. DRC testi bu hataları yakalar.",
        "<b>10. 90 Derece Dik Köşeli Yol Çizme:</b> Yolları dönerken 90 derece yerine daima 45 derece kırarak dön. Dik köşeler yüksek frekansta empedans süreksizliği ve yansıma yapar."
    ]
    for r in rules:
        story.append(Paragraph(r, bullet_style))

    story.append(Spacer(1, 14))

    # =========================================================================
    # 6. BÖLÜM: ADIM ADIM İLK SİPARİŞ VE TEST (BRING-UP)
    # =========================================================================
    story.append(Paragraph("6. Sıfırdan Üretime ve İlk Kart Açılışına (Bring-Up)", h1_style))
    story.append(Paragraph(
        "Kartı çizip bitirdikten sonra fabrikaya gönderme ve masada ilk çalıştırma adımları:", body_style
    ))

    bringup_steps = [
        "<b>Adım 1 - Fabrication Toolkit ile Dosya Üretimi:</b> KiCad eklentisiyle tek tıkla <i>Gerber.zip</i>, <i>BOM.csv</i> ve <i>CPL.csv</i> oluşturulur.",
        "<b>Adım 2 - JLCPCB / PCBWay Siparişi:</b> Kart kalınlığı 1.6 mm, 4 Layer (JLC2313), Renk: Mat Siyah (askeri görünüm için). SMT Assembly seçilerek SMD parçaların fabrikada dizilmesi sağlanır.",
        "<b>Adım 3 - İlk Gözle Muayene (Duman Testinden Önce):</b> Kart eline geldiğinde büyüteçle lehim köprüsü veya kısa devre var mı bakılır.",
        "<b>Adım 4 - Soğuk Multimetre Ölçümü:</b> Cihaza elektrik vermeden multimetre 'Diyot / Kısa Devre Bip' moduna alınır. 3.3V ile GND ve 3.0V ile GND arası ölçülür (Kısa devre OLMAMALIDIR).",
        "<b>Adım 5 - Akım Korumalı İlk Enerji:</b> Laboratuvar güç kaynağı 3.7V ve 100 mA akım sınırına ayarlanıp bağlanır. Multimetreyle voltajların tam 3.3V ve 3.0V olduğu doğrulanır.",
        "<b>Adım 6 - ST-Link ile İlk Bağlantı:</b> SWD kablosu takılır, STM32CubeProgrammer açılır ve 'Connect'e basılır. Cortex-M33 ID'si ekranda görüldüğü an kart hayat bulmuştur!"
    ]
    for bs in bringup_steps:
        story.append(Paragraph(bs, bullet_style))

    story.append(Spacer(1, 14))
    story.append(HRFlowable(width="100%", thickness=1, color=ORANGE, spaceBefore=4, spaceAfter=8))
    story.append(Paragraph(
        "<b>Sonuç:</b> Bu reçete ve yönergeler takip edildiğinde, elindeki C kodları (Kalman filtresi, LoRa mesh, "
        "MIP display HUD, acil durum çığ dedektörü) fiziksel dünyada en zorlu kutup ve dağ koşullarında dahi "
        "kusursuz çalışan gerçek bir askeri/taktik seyrüsefer terminaline dönüşecektir.",
        body_style
    ))

    doc.build(story, canvasmaker=NumberedCanvas)
    print(f"PDF basariyla olusturuldu: {output_path}")

if __name__ == '__main__':
    out_dir = r"c:\Users\ayibogan996\Desktop\projeler\gps\docs"
    os.makedirs(out_dir, exist_ok=True)
    target_pdf = os.path.join(out_dir, "EKSTREM_GNSS_DEVRE_VE_PCB_TASARIM_REHBERI.pdf")
    create_guide_pdf(target_pdf)
