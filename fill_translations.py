#!/usr/bin/env python3
"""Fill translations for all .ts files except en/es."""

import os
import re
import json
from pathlib import Path

TRANSLATIONS_DIR = Path(__file__).parent / "translations"
EN_FILE = TRANSLATIONS_DIR / "zipfx_en.ts"

LANGUAGES = [
    ("fr", "fr_FR", "French"), ("de", "de_DE", "German"),
    ("it", "it_IT", "Italian"), ("pt", "pt_PT", "Portuguese"),
    ("nl", "nl_NL", "Dutch"), ("sv", "sv_SE", "Swedish"),
    ("no", "nb_NO", "Norwegian"), ("da", "da_DK", "Danish"),
    ("fi", "fi_FI", "Finnish"), ("ru", "ru_RU", "Russian"),
    ("ja", "ja_JP", "Japanese"), ("zh", "zh_CN", "Chinese"),
    ("ko", "ko_KR", "Korean"), ("ar", "ar_SA", "Arabic"),
]

# Translation dictionary: source_text -> {lang_code: translation}
T = {}

def add(src, **kwargs):
    T[src] = kwargs

# ========== TRANSLATION DATA ==========

add("New Archive", fr="Nouvelle archive", de="Neues Archiv", it="Nuovo archivio",
    pt="Nova archive", nl="Nieuw archief", sv="Nytt arkiv",
    no="Nytt arkiv", da="Nyt arkiv", fi="Uusi arkisto",
    ru="Новый архив", ja="新しいアーカイブ", zh="新建归档",
    ko="새 아카이브", ar="أرشيف جديد")

add("Source files / folders to compress:",
    fr="Fichiers / dossiers source à compresser :",
    de="Quelldateien / -ordner zum Komprimieren:",
    it="File/cartelle di origine da comprimere:",
    pt="Ficheiros / pastas de origem para comprimir:",
    nl="Bronbestanden / -mappen om te comprimeren:",
    sv="Källfiler / mappar att komprimera:",
    no="Kilde-filer / mapper å komprimere:",
    da="Kilde-filer / mapper til at komprimere:",
    fi="Lähdetiedostot / -kansiot pakattavaksi:",
    ru="Исходные файлы/папки для сжатия:",
    ja="圧縮するソースファイル／フォルダ：",
    zh="要压缩的源文件/文件夹：",
    ko="압축할 소스 파일/폴더:",
    ar="ملفات/مجلدات المصدر لضغطها:")

add("None selected", fr="Aucun sélectionné", de="Keine ausgewählt",
    it="Nessuno selezionato", pt="Nenhum selecionado",
    nl="Geen geselecteerd", sv="Inget valt", no="Ingen valgt",
    da="Intet valgt", fi="Mitään ei valittu",
    ru="Ничего не выбрано", ja="選択なし", zh="未选择任何内容",
    ko="선택 없음", ar="لم يتم تحديد أي شيء")

add("Add Files...", fr="Ajouter des fichiers...", de="Dateien hinzufügen...",
    it="Aggiungi file...", pt="Adicionar ficheiros...",
    nl="Bestanden toevoegen...", sv="Lägg till filer...",
    no="Legg til filer...", da="Tilføj filer...", fi="Lisää tiedostoja...",
    ru="Добавить файлы...", ja="ファイルを追加...", zh="添加文件...",
    ko="파일 추가...", ar="إضافة ملفات...")

add("Add Folder...", fr="Ajouter un dossier...", de="Ordner hinzufügen...",
    it="Aggiungi cartella...", pt="Adicionar pasta...",
    nl="Map toevoegen...", sv="Lägg till mapp...",
    no="Legg til mappe...", da="Tilføj mappe...", fi="Lisää kansio...",
    ru="Добавить папку...", ja="フォルダを追加...", zh="添加文件夹...",
    ko="폴더 추가...", ar="إضافة مجلد...")

add("Clear", fr="Effacer", de="Löschen", it="Pulisci", pt="Limpar",
    nl="Wissen", sv="Rensa", no="Tøm", da="Ryd", fi="Tyhjennä",
    ru="Очистить", ja="クリア", zh="清除", ko="지우기", ar="مسح")

add("Save as:", fr="Enregistrer sous :", de="Speichern als:",
    it="Salva come:", pt="Guardar como:", nl="Opslaan als:",
    sv="Spara som:", no="Lagre som:", da="Gem som:",
    fi="Tallenna nimellä:", ru="Сохранить как:",
    ja="名前を付けて保存:", zh="另存为：",
    ko="다른 이름으로 저장:", ar="حفظ باسم:")

add("Browse...", fr="Parcourir...", de="Durchsuchen...", it="Sfoglia...",
    pt="Procurar...", nl="Bladeren...", sv="Bläddra...",
    no="Bla gjennom...", da="Gennemse...", fi="Selaa...",
    ru="Обзор...", ja="参照...", zh="浏览...",
    ko="찾아보기...", ar="استعراض...")

add("Format:", fr="Format :", de="Format:", it="Formato:", pt="Formato:",
    nl="Formaat:", sv="Format:", no="Format:", da="Format:", fi="Muoto:",
    ru="Формат:", ja="フォーマット：", zh="格式：", ko="형식:", ar="تنسيق:")

add("Compression:", fr="Compression :", de="Kompression:", it="Compressione:",
    pt="Compressão:", nl="Compressie:", sv="Komprimering:", no="Komprimin:",
    da="Kompression:", fi="Pakkaus:", ru="Сжатие:", ja="圧縮：",
    zh="压缩：", ko="압축:", ar="ضغط:")

add("0 = Store  ·  9 = Maximum",
    fr="0 = Stocker  ·  9 = Maximum", de="0 = Speichern  ·  9 = Maximum",
    it="0 = Archivia  ·  9 = Massimo", pt="0 = Armazenar  ·  9 = Máximo",
    nl="0 = Opslaan  ·  9 = Maximaal", sv="0 = Lagra  ·  9 = Maximal",
    no="0 = Lagre  ·  9 = Maksimal", da="0 = Gem  ·  9 = Maksimal",
    fi="0 = Tallenna  ·  9 = Maksimi", ru="0 = Хранить  ·  9 = Максимум",
    ja="0 = 保存  ·  9 = 最大", zh="0 = 存储  ·  9 = 最大",
    ko="0 = 저장  ·  9 = 최대", ar="0 = تخزين  ·  9 = أقصى")

add("Encryption", fr="Chiffrement", de="Verschlüsselung", it="Crittografia",
    pt="Criptografia", nl="Versleuteling", sv="Kryptering",
    no="Kryptering", da="Kryptering", fi="Salaus",
    ru="Шифрование", ja="暗号化", zh="加密", ko="암호화", ar="تشفير")

add("No password", fr="Aucun mot de passe", de="Kein Passwort",
    it="Nessuna password", pt="Sem palavra-passe", nl="Geen wachtwoord",
    sv="Inget lösenord", no="Inget passord", da="Ingen adgangskode",
    fi="Ei salasanaa", ru="Без пароля", ja="パスワードなし",
    zh="无密码", ko="비밀번호 없음", ar="لا توجد كلمة مرور")

add("Password:", fr="Mot de passe :", de="Passwort:", it="Password:",
    pt="Palavra-passe:", nl="Wachtwoord:", sv="Lösenord:", no="Passord:",
    da="Adgangskode:", fi="Salasana:", ru="Пароль:",
    ja="パスワード：", zh="密码：", ko="비밀번호:", ar="كلمة المرور:")

add("Encrypt file names",
    fr="Chiffrer les noms de fichiers", de="Dateinamen verschlüsseln",
    it="Crittografa nomi file", pt="Criptografar nomes de ficheiros",
    nl="Bestandsnamen versleutelen", sv="Kryptera filnamn",
    no="Krypter filnavn", da="Krypter filnavne", fi="Salaa tiedostonimet",
    ru="Шифровать имена файлов", ja="ファイル名を暗号化", zh="加密文件名",
    ko="파일 이름 암호화", ar="تشفير أسماء الملفات")

add("Only supported by 7z format",
    fr="Uniquement pris en charge par le format 7z",
    de="Nur vom 7z-Format unterstützt",
    it="Supportato solo dal formato 7z",
    pt="Apenas suportado pelo formato 7z",
    nl="Alleen ondersteund door 7z-formaat",
    sv="Stöds endast av 7z-format", no="Bare støttet av 7z-format",
    da="Kun understøttet af 7z-format", fi="Vain 7z-muodon tukema",
    ru="Поддерживается только форматом 7z", ja="7z形式のみ対応",
    zh="仅 7z 格式支持", ko="7z 형식만 지원",
    ar="مدعوم فقط بواسطة تنسيق 7z")

add("Volumes (split)", fr="Volumes (fractionnement)",
    de="Volumes (aufteilen)", it="Volumi (suddivisione)",
    pt="Volumes (dividir)", nl="Volumes (splitsen)",
    sv="Volymer (dela upp)", no="Volumer (del opp)",
    da="Volumener (opdeling)", fi="Talt (jakaminen)",
    ru="Тома (разделение)", ja="ボリューム（分割）", zh="卷（拆分）",
    ko="볼륨 (분할)", ar="وحدات تخزين (تقسيم)")

add("Volume size:", fr="Taille du volume :", de="Volumengröße:",
    it="Dimensione volume:", pt="Tamanho do volume:",
    nl="Volume grootte:", sv="Volymstorlek:", no="Volumstørrelse:",
    da="Volumenstørrelse:", fi="Tallen koko:", ru="Размер тома:",
    ja="ボリュームサイズ：", zh="卷大小：", ko="볼륨 크기:", ar="حجم الوحدة:")

add(" MB", fr=" Mo", de=" MB", it=" MB", pt=" MB", nl=" MB",
    sv=" MB", no=" MB", da=" MB", fi=" Mt", ru="МБ",
    ja="MB", zh="MB", ko="MB", ar="ميجابايت")

add("None", fr="Aucun", de="Keine", it="Nessuno", pt="Nenhum",
    nl="Geen", sv="Ingen", no="Ingen", da="Ingen", fi="Tyhjä",
    ru="Нет", ja="なし", zh="无", ko="없음", ar="لا شيء")

add("Please choose a destination path.",
    fr="Veuillez choisir un chemin de destination.",
    de="Bitte wählen Sie einen Zielpfad.",
    it="Scegliere un percorso di destinazione.",
    pt="Por favor, escolha um caminho de destino.",
    nl="Kies een bestemmingspad.", sv="Välj en destinationssökväg.",
    no="Velg en destinasjonsbane.", da="Vælg en destinationssti.",
    fi="Valitse kohdepolku.", ru="Выберите путь назначения.",
    ja="保存先のパスを選択してください。", zh="请选择目标路径。",
    ko="대상 경로를 선택하십시오.", ar="الرجاء اختيار مسار الوجهة.")

add("Create", fr="Créer", de="Erstellen", it="Crea", pt="Criar",
    nl="Aanmaken", sv="Skapa", no="Opprett", da="Opret", fi="Luo",
    ru="Создать", ja="作成", zh="创建", ko="만들기", ar="إنشاء")

add("Cancel", fr="Annuler", de="Abbrechen", it="Annulla", pt="Cancelar",
    nl="Annuleren", sv="Avbryt", no="Avbryt", da="Annuller", fi="Peruuta",
    ru="Отмена", ja="キャンセル", zh="取消", ko="취소", ar="إلغاء")

add("Select files to compress",
    fr="Sélectionner les fichiers à compresser",
    de="Dateien zum Komprimieren auswählen",
    it="Seleziona file da comprimere",
    pt="Selecionar ficheiros para comprimir",
    nl="Selecteer bestanden om te comprimeren",
    sv="Välj filer att komprimera", no="Velg filer å komprimere",
    da="Vælg filer at komprimere", fi="Valitse pakattavat tiedostot",
    ru="Выберите файлы для сжатия", ja="圧縮するファイルを選択",
    zh="选择要压缩的文件", ko="압축할 파일 선택", ar="حدد الملفات لضغطها")

add("Select folder to compress",
    fr="Sélectionner le dossier à compresser",
    de="Ordner zum Komprimieren auswählen",
    it="Seleziona cartella da comprimere",
    pt="Selecionar pasta para comprimir",
    nl="Selecteer map om te comprimeren", sv="Välj mapp att komprimera",
    no="Velg mappe å komprimere", da="Vælg mappe at komprimere",
    fi="Valitse kansio pakattavaksi", ru="Выберите папку для сжатия",
    ja="圧縮するフォルダを選択", zh="选择要压缩的文件夹",
    ko="압축할 폴더 선택", ar="حدد المجلد لضغطه")

add("%1 file(s)", fr="%1 fichier(s)", de="%1 Datei(en)", it="%1 file",
    pt="%1 ficheiro(s)", nl="%1 bestand(en)", sv="%1 fil(er)",
    no="%1 fil(er)", da="%1 fil(er)", fi="%1 tiedosto(a)",
    ru="%1 файл(ов)", ja="%1 ファイル", zh="%1 个文件",
    ko="%1 파일", ar="%1 ملف(ات)")

add("%1 folder(s)", fr="%1 dossier(s)", de="%1 Ordner", it="%1 cartella(e)",
    pt="%1 pasta(s)", nl="%1 map(pen)", sv="%1 mapp(ar)",
    no="%1 mappe(r)", da="%1 mappe(r)", fi="%1 kansio(ta)",
    ru="%1 папок(и)", ja="%1 フォルダ", zh="%1 个文件夹",
    ko="%1 폴더", ar="%1 مجلد(ات)")

add(", ", fr=", ", de=", ", it=", ", pt=", ", nl=", ", sv=", ",
    no=", ", da=", ", fi=", ", ru=", ", ja="、", zh="，",
    ko=", ", ar="، ")

add("Save Archive", fr="Enregistrer l'archive", de="Archiv speichern",
    it="Salva archivio", pt="Guardar archive", nl="Archief opslaan",
    sv="Spara arkiv", no="Lagre arkiv", da="Gem arkiv", fi="Tallenna arkisto",
    ru="Сохранить архив", ja="アーカイブを保存", zh="保存归档",
    ko="아카이브 저장", ar="حفظ الأرشيف")

add("%1 (*%2)", fr="%1 (*%2)", de="%1 (*%2)", it="%1 (*%2)", pt="%1 (*%2)",
    nl="%1 (*%2)", sv="%1 (*%2)", no="%1 (*%2)", da="%1 (*%2)",
    fi="%1 (*%2)", ru="%1 (*%2)", ja="%1 (*%2)", zh="%1 (*%2)",
    ko="%1 (*%2)", ar="%1 (*%2)")

add("Error", fr="Erreur", de="Fehler", it="Errore", pt="Erro", nl="Fout",
    sv="Fel", no="Feil", da="Fejl", fi="Virhe", ru="Ошибка", ja="エラー",
    zh="错误", ko="오류", ar="خطأ")

add("Please choose a path.", fr="Veuillez choisir un chemin.",
    de="Bitte wählen Sie einen Pfad.", it="Scegliere un percorso.",
    pt="Por favor, escolha um caminho.", nl="Kies een pad.",
    sv="Välj en sökväg.", no="Velg en bane.", da="Vælg en sti.",
    fi="Valitse polku.", ru="Выберите путь.",
    ja="パスを選択してください。", zh="请选择路径。",
    ko="경로를 선택하십시오.", ar="الرجاء اختيار مسار.")

add("Extracting files...",
    fr="Extraction des fichiers...", de="Dateien werden extrahiert...",
    it="Estrazione file in corso...", pt="A extrair ficheiros...",
    nl="Bestanden uitpakken...", sv="Extraherar filer...",
    no="Pakker ut filer...", da="Udpakker filer...",
    fi="Purkaminen käynnissä...", ru="Извлечение файлов...",
    ja="ファイルを展開中...", zh="正在提取文件...",
    ko="파일 추출 중...", ar="جاري استخراج الملفات...")

add("After:", fr="Après :", de="Nachher:", it="Dopo:", pt="Após:",
    nl="Na:", sv="Efter:", no="Etter:", da="Efter:", fi="Jälkeen:",
    ru="После:", ja="後：", zh="之后：", ko="후:", ar="بعد:")

add("Folder", fr="Dossier", de="Ordner", it="Cartella", pt="Pasta",
    nl="Map", sv="Mapp", no="Mappe", da="Mappe", fi="Kansio",
    ru="Папка", ja="フォルダ", zh="文件夹", ko="폴더", ar="مجلد")

add("File", fr="Fichier", de="Datei", it="File", pt="Ficheiro",
    nl="Bestand", sv="Fil", no="Fil", da="Fil", fi="Tiedosto",
    ru="Файл", ja="ファイル", zh="文件", ko="파일", ar="ملف")

add("Name", fr="Nom", de="Name", it="Nome", pt="Nome", nl="Naam",
    sv="Namn", no="Navn", da="Navn", fi="Nimi", ru="Имя", ja="名前",
    zh="名称", ko="이름", ar="الاسم")

add("Size", fr="Taille", de="Größe", it="Dimensione", pt="Tamanho",
    nl="Grootte", sv="Storlek", no="Størrelse", da="Størrelse",
    fi="Koko", ru="Размер", ja="サイズ", zh="大小", ko="크기", ar="الحجم")

add("Packed", fr="Compressé", de="Komprimiert", it="Compresso",
    pt="Comprimido", nl="Gecomprimeerd", sv="Packad", no="Pakket",
    da="Pakket", fi="Pakattu", ru="Сжатый", ja="圧縮後",
    zh="压缩后", ko="압축됨", ar="مضغوط")

add("Type", fr="Type", de="Typ", it="Tipo", pt="Tipo", nl="Type",
    sv="Typ", no="Type", da="Type", fi="Tyyppi", ru="Тип",
    ja="種類", zh="类型", ko="종류", ar="النوع")

add("Modified", fr="Modifié", de="Geändert", it="Modificato",
    pt="Modificado", nl="Gewijzigd", sv="Ändrad", no="Endret",
    da="Ændret", fi="Muokattu", ru="Изменён", ja="変更日時",
    zh="修改时间", ko="수정됨", ar="تم التعديل")

add("CRC", fr="CRC", de="CRC", it="CRC", pt="CRC", nl="CRC",
    sv="CRC", no="CRC", da="CRC", fi="CRC", ru="CRC", ja="CRC",
    zh="CRC", ko="CRC", ar="CRC")

add("Permissions", fr="Permissions", de="Berechtigungen", it="Permessi",
    pt="Permissões", nl="Machtigingen", sv="Behörigheter",
    no="Tillatelser", da="Tilladelser", fi="Käyttöoikeudet",
    ru="Разрешения", ja="許可", zh="权限", ko="권한", ar="الأذونات")

add("ZipFX", fr="ZipFX", de="ZipFX", it="ZipFX", pt="ZipFX", nl="ZipFX",
    sv="ZipFX", no="ZipFX", da="ZipFX", fi="ZipFX", ru="ZipFX", ja="ZipFX",
    zh="ZipFX", ko="ZipFX", ar="ZipFX")

add("Ready", fr="Prêt", de="Bereit", it="Pronto", pt="Pronto", nl="Gereed",
    sv="Redo", no="Klar", da="Klar", fi="Valmis", ru="Готов",
    ja="準備完了", zh="就绪", ko="준비", ar="جاهز")

add("Tools", fr="Outils", de="Werkzeuge", it="Strumenti", pt="Ferramentas",
    nl="Gereedschap", sv="Verktyg", no="Verktøy", da="Værktøjer",
    fi="Työkalut", ru="Инструменты", ja="ツール", zh="工具",
    ko="도구", ar="أدوات")

add("Add", fr="Ajouter", de="Hinzufügen", it="Aggiungi", pt="Adicionar",
    nl="Toevoegen", sv="Lägg till", no="Legg til", da="Tilføj",
    fi="Lisää", ru="Добавить", ja="追加", zh="添加", ko="추가", ar="إضافة")

add("Extract To", fr="Extraire vers", de="Extrahieren nach", it="Estrai in",
    pt="Extrair para", nl="Uitpakken naar", sv="Extrahera till",
    no="Pakk ut til", da="Udpak til", fi="Pura kohteeseen",
    ru="Извлечь в", ja="展開先", zh="提取到", ko="추출 대상",
    ar="استخراج إلى")

add("Test", fr="Tester", de="Prüfen", it="Test", pt="Testar", nl="Testen",
    sv="Testa", no="Test", da="Test", fi="Testaa", ru="Тест", ja="テスト",
    zh="测试", ko="테스트", ar="اختبار")

add("View", fr="Voir", de="Ansicht", it="Visualizza", pt="Ver", nl="Weergave",
    sv="Visa", no="Vis", da="Vis", fi="Näytä", ru="Просмотр", ja="表示",
    zh="查看", ko="보기", ar="عرض")

add("Delete", fr="Supprimer", de="Löschen", it="Elimina", pt="Eliminar",
    nl="Verwijderen", sv="Ta bort", no="Slett", da="Slet", fi="Poista",
    ru="Удалить", ja="削除", zh="删除", ko="삭제", ar="حذف")

add("Close", fr="Fermer", de="Schließen", it="Chiudi", pt="Fechar",
    nl="Sluiten", sv="Stäng", no="Lukk", da="Luk", fi="Sulje",
    ru="Закрыть", ja="閉じる", zh="关闭", ko="닫기", ar="إغلاق")

add("Find", fr="Rechercher", de="Suchen", it="Cerca", pt="Procurar",
    nl="Zoeken", sv="Sök", no="Søk", da="Søg", fi="Etsi",
    ru="Найти", ja="検索", zh="查找", ko="찾기", ar="بحث")

add("Info", fr="Infos", de="Info", it="Info", pt="Informações", nl="Info",
    sv="Info", no="Info", da="Info", fi="Tiedot", ru="Инфо",
    ja="情報", zh="信息", ko="정보", ar="معلومات")

add("Address:", fr="Adresse :", de="Adresse:", it="Indirizzo:",
    pt="Endereço:", nl="Adres:", sv="Adress:", no="Adresse:",
    da="Adresse:", fi="Osoite:", ru="Адрес:", ja="アドレス：",
    zh="地址：", ko="주소:", ar="العنوان:")

add("Format not supported.",
    fr="Format non pris en charge.", de="Format nicht unterstützt.",
    it="Formato non supportato.", pt="Formato não suportado.",
    nl="Formaat niet ondersteund.", sv="Formatet stöds inte.",
    no="Formatet støttes ikke.", da="Formatet understøttes ikke.",
    fi="Muotoa ei tueta.", ru="Формат не поддерживается.",
    ja="サポートされていない形式です。", zh="不支持的格式。",
    ko="지원되지 않는 형식입니다.", ar="التنسيق غير مدعوم.")

add("Could not create archive.",
    fr="Impossible de créer l'archive.", de="Archiv konnte nicht erstellt werden.",
    it="Impossibile creare l'archivio.", pt="Não foi possível criar o archive.",
    nl="Kon archief niet aanmaken.", sv="Kunde inte skapa arkiv.",
    no="Kunne ikke opprette arkiv.", da="Kunne ikke oprette arkiv.",
    fi="Arkistoa ei voitu luoda.", ru="Не удалось создать архив.",
    ja="アーカイブを作成できませんでした。", zh="无法创建归档。",
    ko="아카이브를 만들 수 없습니다.", ar="تعذر إنشاء الأرشيف.")

add("Archive created", fr="Archive créée", de="Archiv erstellt",
    it="Archivio creato", pt="Archive criado", nl="Archief aangemaakt",
    sv="Arkiv skapat", no="Arkiv opprettet", da="Arkiv oprettet",
    fi="Arkisto luotu", ru="Архив создан", ja="アーカイブを作成しました",
    zh="归档已创建", ko="아카이브가 생성되었습니다", ar="تم إنشاء الأرشيف")

add("Open Archive", fr="Ouvrir l'archive", de="Archiv öffnen",
    it="Apri archivio", pt="Abrir archive", nl="Archief openen",
    sv="Öppna arkiv", no="Åpne arkiv", da="Åbn arkiv", fi="Avaa arkisto",
    ru="Открыть архив", ja="アーカイブを開く", zh="打开归档",
    ko="아카이브 열기", ar="فتح الأرشيف")

add("Open Failed", fr="Échec d'ouverture", de="Öffnen fehlgeschlagen",
    it="Apertura fallita", pt="Falha ao abrir", nl="Openen mislukt",
    sv="Öppning misslyckades", no="Åpning mislyktes",
    da="Åbning mislykkedes", fi="Avaaminen epäonnistui",
    ru="Не удалось открыть", ja="開けませんでした",
    zh="打开失败", ko="열기 실패", ar="فشل الفتح")

add("Could not open the archive.",
    fr="Impossible d'ouvrir l'archive.",
    de="Archiv konnte nicht geöffnet werden.",
    it="Impossibile aprire l'archivio.",
    pt="Não foi possível abrir o archive.",
    nl="Kon archief niet openen.", sv="Kunde inte öppna arkivet.",
    no="Kunne ikke åpne arkivet.", da="Kunne ikke åbne arkivet.",
    fi="Arkistoa ei voitu avata.", ru="Не удалось открыть архив.",
    ja="アーカイブを開けませんでした。", zh="无法打开归档。",
    ko="아카이브를 열 수 없습니다.", ar="تعذر فتح الأرشيف.")

add("Opened: %1", fr="Ouvert : %1", de="Geöffnet: %1", it="Aperto: %1",
    pt="Aberto: %1", nl="Geopend: %1", sv="Öppnade: %1", no="Åpnet: %1",
    da="Åbnede: %1", fi="Avattu: %1", ru="Открыто: %1",
    ja="開きました: %1", zh="已打开：%1", ko="열림: %1", ar="تم الفتح: %1")

add("No archive open", fr="Aucune archive ouverte", de="Kein Archiv geöffnet",
    it="Nessun archivio aperto", pt="Nenhum archive aberto",
    nl="Geen archief geopend", sv="Inget arkiv öppnat",
    no="Intet arkiv åpent", da="Intet arkiv åbent", fi="Ei arkistoa auki",
    ru="Нет открытых архивов", ja="開いているアーカイブはありません",
    zh="没有打开的归档", ko="열린 아카이브 없음", ar="لا يوجد أرشيف مفتوح")

add("No archive open or read-only.",
    fr="Aucune archive ouverte ou lecture seule.",
    de="Kein Archiv geöffnet oder schreibgeschützt.",
    it="Nessun archivio aperto o sola lettura.",
    pt="Nenhum archive aberto ou só de leitura.",
    nl="Geen archief geopend of alleen-lezen.",
    sv="Inget arkiv öppnat eller skrivskyddat.",
    no="Intet arkiv åpent eller skrivebeskyttet.",
    da="Intet arkiv åbent eller skrivebeskyttet.",
    fi="Ei arkistoa auki tai vain luku -tilassa.",
    ru="Нет открытых архивов или только чтение.",
    ja="開いているアーカイブがないか、読み取り専用です。",
    zh="没有打开的归档或只读。",
    ko="열린 아카이브가 없거나 읽기 전용입니다.",
    ar="لا يوجد أرشيف مفتوح أو للقراءة فقط.")

add("Add Files", fr="Ajouter des fichiers", de="Dateien hinzufügen",
    it="Aggiungi file", pt="Adicionar ficheiros", nl="Bestanden toevoegen",
    sv="Lägg till filer", no="Legg til filer", da="Tilføj filer",
    fi="Lisää tiedostoja", ru="Добавить файлы", ja="ファイルを追加",
    zh="添加文件", ko="파일 추가", ar="إضافة ملفات")

add("Adding files...", fr="Ajout des fichiers...",
    de="Dateien werden hinzugefügt...", it="Aggiunta file in corso...",
    pt="A adicionar ficheiros...", nl="Bestanden toevoegen...",
    sv="Lägger till filer...", no="Legger til filer...",
    da="Tilføjer filer...", fi="Lisätään tiedostoja...",
    ru="Добавление файлов...", ja="ファイルを追加中...",
    zh="正在添加文件...", ko="파일 추가 중...", ar="جاري إضافة الملفات...")

add("Adding: %1", fr="Ajout : %1", de="Füge hinzu: %1", it="Aggiunta: %1",
    pt="A adicionar: %1", nl="Bezig met toevoegen: %1",
    sv="Lägger till: %1", no="Legger til: %1", da="Tilføjer: %1",
    fi="Lisätään: %1", ru="Добавление: %1", ja="追加中: %1",
    zh="正在添加：%1", ko="추가 중: %1", ar="جاري الإضافة: %1")

add("Saving...", fr="Enregistrement...", de="Speichern...",
    it="Salvataggio in corso...", pt="A guardar...", nl="Opslaan...",
    sv="Sparar...", no="Lagrer...", da="Gemmer...", fi="Tallennetaan...",
    ru="Сохранение...", ja="保存中...", zh="正在保存...",
    ko="저장 중...", ar="جاري الحفظ...")

add("ZipFX %1", fr="ZipFX %1", de="ZipFX %1", it="ZipFX %1", pt="ZipFX %1",
    nl="ZipFX %1", sv="ZipFX %1", no="ZipFX %1", da="ZipFX %1",
    fi="ZipFX %1", ru="ZipFX %1", ja="ZipFX %1", zh="ZipFX %1",
    ko="ZipFX %1", ar="ZipFX %1")

add("Language", fr="Langue", de="Sprache", it="Lingua", pt="Idioma",
    nl="Taal", sv="Språk", no="Språk", da="Sprog", fi="Kieli",
    ru="Язык", ja="言語", zh="语言", ko="언어", ar="اللغة")

add("Add Folder", fr="Ajouter un dossier", de="Ordner hinzufügen",
    it="Aggiungi cartella", pt="Adicionar pasta", nl="Map toevoegen",
    sv="Lägg till mapp", no="Legg til mappe", da="Tilføj mappe",
    fi="Lisää kansio", ru="Добавить папку", ja="フォルダを追加",
    zh="添加文件夹", ko="폴더 추가", ar="إضافة مجلد")

add("Go to parent directory",
    fr="Aller au dossier parent", de="Zum übergeordneten Ordner",
    it="Vai alla cartella superiore", pt="Ir para a pasta pai",
    nl="Naar bovenliggende map", sv="Gå till överordnad mapp",
    no="Gå til overordnet mappe", da="Gå til overordnet mappe",
    fi="Siirrä yläkansioon", ru="Перейти в родительскую папку",
    ja="親ディレクトリへ移動", zh="转到父目录",
    ko="상위 디렉토리로 이동", ar="الانتقال إلى المجلد الأصل")

add("Search files...", fr="Rechercher des fichiers...",
    de="Dateien suchen...", it="Cerca file...", pt="Procurar ficheiros...",
    nl="Zoek bestanden...", sv="Sök filer...", no="Søk etter filer...",
    da="Søg efter filer...", fi="Etsi tiedostoja...",
    ru="Поиск файлов...", ja="ファイルを検索...", zh="搜索文件...",
    ko="파일 검색...", ar="البحث عن ملفات...")

add("Failed to save archive.",
    fr="Échec de l'enregistrement de l'archive.",
    de="Archiv konnte nicht gespeichert werden.",
    it="Salvataggio archivio fallito.", pt="Falha ao guardar o archive.",
    nl="Opslaan van archief mislukt.", sv="Kunde inte spara arkivet.",
    no="Kunne ikke lagre arkivet.", da="Kunne ikke gemme arkivet.",
    fi="Arkiston tallennus epäonnistui.", ru="Не удалось сохранить архив.",
    ja="アーカイブの保存に失敗しました。", zh="保存归档失败。",
    ko="아카이브 저장에 실패했습니다.", ar="فشل حفظ الأرشيف.")

add("Extract to", fr="Extraire vers", de="Extrahieren nach", it="Estrai in",
    pt="Extrair para", nl="Uitpakken naar", sv="Extrahera till",
    no="Pakk ut til", da="Udpak til", fi="Pura kohteeseen",
    ru="Извлечь в", ja="展開先", zh="提取到", ko="추출 대상",
    ar="استخراج إلى")

add("Select files first.", fr="Sélectionnez d'abord des fichiers.",
    de="Wählen Sie zuerst Dateien aus.", it="Selezionare prima i file.",
    pt="Selecione primeiro os ficheiros.", nl="Selecteer eerst bestanden.",
    sv="Välj filer först.", no="Velg filer først.", da="Vælg filer først.",
    fi="Valitse tiedostot ensin.", ru="Сначала выберите файлы.",
    ja="最初にファイルを選択してください。", zh="请先选择文件。",
    ko="먼저 파일을 선택하십시오.", ar="حدد الملفات أولاً.")

add("Archive is empty.", fr="L'archive est vide.", de="Archiv ist leer.",
    it="L'archivio è vuoto.", pt="O archive está vazio.",
    nl="Archief is leeg.", sv="Arkivet är tomt.", no="Arkivet er tomt.",
    da="Arkivet er tomt.", fi="Arkisto on tyhjä.",
    ru="Архив пуст.", ja="アーカイブは空です。", zh="归档为空。",
    ko="아카이브가 비어 있습니다.", ar="الأرشيف فارغ.")

add("Extracting...", fr="Extraction en cours...", de="Extrahiere...",
    it="Estrazione in corso...", pt="A extrair...",
    nl="Bezig met uitpakken...", sv="Extraherar...", no="Pakker ut...",
    da="Udpakker...", fi="Purkaminen...", ru="Извлечение...",
    ja="展開中...", zh="正在提取...", ko="추출 중...", ar="جاري الاستخراج...")

add("Extracting: %1", fr="Extraction : %1", de="Extrahiere: %1",
    it="Estrazione: %1", pt="A extrair: %1", nl="Bezig met uitpakken: %1",
    sv="Extraherar: %1", no="Pakker ut: %1", da="Udpakker: %1",
    fi="Puretaan: %1", ru="Извлечение: %1", ja="展開中: %1",
    zh="正在提取：%1", ko="추출 중: %1", ar="جاري الاستخراج: %1")

add("Overwrite?", fr="Remplacer ?", de="Überschreiben?", it="Sovrascrivere?",
    pt="Sobrescrever?", nl="Overschrijven?", sv="Skriva över?",
    no="Overskrive?", da="Overskriv?", fi="Korvataanko?",
    ru="Перезаписать?", ja="上書きしますか？", zh="覆盖？",
    ko="덮어쓰시겠습니까?", ar="كتابة فوق؟")

add("Extraction complete", fr="Extraction terminée",
    de="Extraktion abgeschlossen", it="Estrazione completata",
    pt="Extração concluída", nl="Uitpakken voltooid",
    sv="Extrahering klar", no="Utpakking fullført",
    da="Udpakning fuldført", fi="Purkaminen valmis",
    ru="Извлечение завершено", ja="展開完了", zh="提取完成",
    ko="추출 완료", ar="اكتمل الاستخراج")

add("Extract selected to", fr="Extraire la sélection vers",
    de="Auswahl extrahieren nach", it="Estrai selezione in",
    pt="Extrair selecionados para", nl="Selectie uitpakken naar",
    sv="Extrahera valda till", no="Pakk ut valgte til",
    da="Udpak valgte til", fi="Pura valitut kohteeseen",
    ru="Извлечь выбранное в", ja="選択項目を展開先",
    zh="将选定内容提取到", ko="선택 항목을 다음으로 추출",
    ar="استخراج المحدد إلى")

add("Testing integrity...", fr="Vérification d'intégrité...",
    de="Integritätsprüfung...", it="Verifica integrità in corso...",
    pt="A testar integridade...", nl="Integriteit controleren...",
    sv="Testar integritet...", no="Tester integritet...",
    da="Tester integritet...", fi="Testataan eheyttä...",
    ru="Проверка целостности...", ja="整合性を確認中...",
    zh="正在检查完整性...", ko="무결성 검사 중...",
    ar="جاري فحص التكامل...")

add("Testing: %1", fr="Test : %1", de="Prüfe: %1", it="Test: %1",
    pt="A testar: %1", nl="Testen: %1", sv="Testar: %1", no="Tester: %1",
    da="Tester: %1", fi="Testataan: %1", ru="Тест: %1",
    ja="テスト中: %1", zh="正在测试：%1", ko="테스트 중: %1",
    ar="جاري الاختبار: %1")

add("Integrity check passed.", fr="Vérification d'intégrité réussie.",
    de="Integritätsprüfung bestanden.", it="Verifica integrità superata.",
    pt="Teste de integridade aprovado.", nl="Integriteitscontrole geslaagd.",
    sv="Integritetskontroll godkänd.", no="Integritetssjekk bestått.",
    da="Integritetskontrol bestået.", fi="Eheystarkistus läpäisty.",
    ru="Проверка целостности пройдена.",
    ja="整合性チェックに合格しました。", zh="完整性检查通过。",
    ko="무결성 검사 통과.", ar="نجح فحص التكامل.")

add("Integrity check cancelled.", fr="Vérification d'intégrité annulée.",
    de="Integritätsprüfung abgebrochen.",
    it="Verifica integrità annullata.", pt="Teste de integridade cancelado.",
    nl="Integriteitscontrole geannuleerd.",
    sv="Integritetskontroll avbruten.", no="Integritetssjekk avbrutt.",
    da="Integritetskontrol annulleret.", fi="Eheystarkistus peruttu.",
    ru="Проверка целостности отменена.",
    ja="整合性チェックがキャンセルされました。",
    zh="完整性检查已取消。", ko="무결성 검사가 취소되었습니다.",
    ar="تم إلغاء فحص التكامل.")

add("Integrity check FAILED.", fr="Vérification d'intégrité ÉCHOUÉE.",
    de="Integritätsprüfung FEHLGESCHLAGEN.",
    it="Verifica integrità FALLITA.", pt="Teste de integridade FALHOU.",
    nl="Integriteitscontrole MISLUKT.",
    sv="Integritetskontroll MISSLYCKADES.",
    no="Integritetssjekk MISLYKTES.",
    da="Integritetskontrol MISLYKKEDES.",
    fi="Eheystarkistus EPÄONNISTUI.",
    ru="Проверка целостности НЕ ПРОЙДЕНА.",
    ja="整合性チェックに失敗しました。", zh="完整性检查失败。",
    ko="무결성 검사 실패.", ar="فشل فحص التكامل.")

add("Could not read file.", fr="Impossible de lire le fichier.",
    de="Datei konnte nicht gelesen werden.",
    it="Impossibile leggere il file.",
    pt="Não foi possível ler o ficheiro.", nl="Kon bestand niet lezen.",
    sv="Kunde inte läsa filen.", no="Kunne ikke lese filen.",
    da="Kunne ikke læse filen.", fi="Tiedostoa ei voitu lukea.",
    ru="Не удалось прочитать файл.",
    ja="ファイルを読み取れませんでした。", zh="无法读取文件。",
    ko="파일을 읽을 수 없습니다.", ar="تعذر قراءة الملف.")

add("View: %1", fr="Voir : %1", de="Ansicht: %1", it="Visualizza: %1",
    pt="Ver: %1", nl="Weergave: %1", sv="Visa: %1", no="Vis: %1",
    da="Vis: %1", fi="Näytä: %1", ru="Просмотр: %1", ja="表示: %1",
    zh="查看：%1", ko="보기: %1", ar="عرض: %1")

add("Confirm", fr="Confirmer", de="Bestätigen", it="Conferma", pt="Confirmar",
    nl="Bevestigen", sv="Bekräfta", no="Bekreft", da="Bekræft",
    fi="Vahvista", ru="Подтвердить", ja="確認", zh="确认", ko="확인",
    ar="تأكيد")

add("Delete %1 files?", fr="Supprimer %1 fichiers ?", de="%1 Dateien löschen?",
    it="Eliminare %1 file?", pt="Eliminar %1 ficheiros?",
    nl="%1 bestanden verwijderen?", sv="Ta bort %1 filer?",
    no="Slett %1 filer?", da="Slet %1 filer?",
    fi="Poistetaanko %1 tiedostoa?", ru="Удалить %1 файл(ов)?",
    ja="%1 ファイルを削除しますか？", zh="删除 %1 个文件？",
    ko="%1 파일을 삭제하시겠습니까?", ar="حذف %1 ملف(ات)؟")

add("Extract...", fr="Extraire...", de="Extrahieren...", it="Estrai...",
    pt="Extrair...", nl="Uitpakken...", sv="Extrahera...", no="Pakk ut...",
    da="Udpak...", fi="Pura...", ru="Извлечь...", ja="展開...",
    zh="提取...", ko="추출...", ar="استخراج...")

add("Rename...", fr="Renommer...", de="Umbenennen...", it="Rinomina...",
    pt="Renomear...", nl="Hernoemen...", sv="Byt namn...",
    no="Gi nytt navn...", da="Omdøb...", fi="Nimeä uudelleen...",
    ru="Переименовать...", ja="名前の変更...", zh="重命名...",
    ko="이름 바꾸기...", ar="إعادة تسمية...")

add("Rename", fr="Renommer", de="Umbenennen", it="Rinomina", pt="Renomear",
    nl="Hernoemen", sv="Byt namn", no="Gi nytt navn", da="Omdøb",
    fi="Nimeä uudelleen", ru="Переименовать", ja="名前の変更",
    zh="重命名", ko="이름 바꾸기", ar="إعادة تسمية")

add("New name for %1:", fr="Nouveau nom pour %1 :",
    de="Neuer Name für %1:", it="Nuovo nome per %1:",
    pt="Novo nome para %1:", nl="Nieuwe naam voor %1:",
    sv="Nytt namn för %1:", no="Nytt navn for %1:",
    da="Nyt navn for %1:", fi="Uusi nimi kohteelle %1:",
    ru="Новое имя для %1:", ja="%1 の新しい名前：",
    zh="%1 的新名称：", ko="%1의 새 이름:", ar="الاسم الجديد لـ %1:")

add("Rename failed.", fr="Échec du renommage.",
    de="Umbenennung fehlgeschlagen.", it="Rinomina fallita.",
    pt="Renomeação falhou.", nl="Hernoemen mislukt.",
    sv="Namnbyte misslyckades.", no="Navneendring mislyktes.",
    da="Omdøbning mislykkedes.", fi="Uudelleennimeäminen epäonnistui.",
    ru="Переименование не удалось.",
    ja="名前の変更に失敗しました。", zh="重命名失败。",
    ko="이름 바꾸기에 실패했습니다.", ar="فشلت إعادة التسمية.")

add("Preparing files for drag...",
    fr="Préparation des fichiers pour le glisser...",
    de="Dateien zum Ziehen vorbereiten...",
    it="Preparazione file per trascinamento...",
    pt="A preparar ficheiros para arrastar...",
    nl="Bestanden voorbereiden voor slepen...",
    sv="Förbereder filer för dragning...",
    no="Forbereder filer for dra og slipp...",
    da="Forbereder filer til træk...",
    fi="Valmistellaan tiedostoja vedettäviksi...",
    ru="Подготовка файлов для перетаскивания...",
    ja="ドラッグ用のファイルを準備中...", zh="正在准备拖放文件...",
    ko="드래그용 파일 준비 중...", ar="جارٍ تحضير الملفات للسحب...")

add("Read-only format.", fr="Format en lecture seule.",
    de="Schreibgeschütztes Format.", it="Formato di sola lettura.",
    pt="Formato só de leitura.", nl="Alleen-lezen formaat.",
    sv="Skrivskyddat format.", no="Skrivebeskyttet format.",
    da="Skrivebeskyttet format.", fi="Vain luku -muoto.",
    ru="Формат только для чтения.", ja="読み取り専用形式です。",
    zh="只读格式。", ko="읽기 전용 형식입니다.", ar="تنسيق للقراءة فقط.")

add("Failed to save.", fr="Échec de l'enregistrement.",
    de="Speichern fehlgeschlagen.", it="Salvataggio fallito.",
    pt="Falha ao guardar.", nl="Opslaan mislukt.",
    sv="Spara misslyckades.", no="Lagring mislyktes.",
    da="Gemning mislykkedes.", fi="Tallennus epäonnistui.",
    ru="Не удалось сохранить.", ja="保存に失敗しました。",
    zh="保存失败。", ko="저장에 실패했습니다.", ar="فشل الحفظ.")

add("%1 files", fr="%1 fichiers", de="%1 Dateien", it="%1 file",
    pt="%1 ficheiros", nl="%1 bestanden", sv="%1 filer",
    no="%1 filer", da="%1 filer", fi="%1 tiedostoa",
    ru="%1 файл(ов)", ja="%1 ファイル", zh="%1 个文件",
    ko="%1 파일", ar="%1 ملف(ات)")

add("ZipFX — %1", fr="ZipFX — %1", de="ZipFX — %1", it="ZipFX — %1",
    pt="ZipFX — %1", nl="ZipFX — %1", sv="ZipFX — %1", no="ZipFX — %1",
    da="ZipFX — %1", fi="ZipFX — %1", ru="ZipFX — %1", ja="ZipFX — %1",
    zh="ZipFX — %1", ko="ZipFX — %1", ar="ZipFX — %1")

add("Do nothing", fr="Ne rien faire", de="Nichts tun", it="Non fare nulla",
    pt="Não fazer nada", nl="Niets doen", sv="Gör ingenting",
    no="Gjør ingenting", da="Gør ingenting", fi="Älä tee mitään",
    ru="Ничего не делать", ja="何もしない", zh="不执行任何操作",
    ko="아무것도 하지 않음", ar="لا تفعل شيئًا")

add("Sleep", fr="Veille", de="Ruhezustand", it="Sospendi", pt="Suspender",
    nl="Slaap", sv="Vila", no="Søvn", da="Søvn", fi="Uni",
    ru="Сон", ja="スリープ", zh="睡眠", ko="절전", ar="سكون")

add("Hibernate", fr="Hibernation", de="Energiesparmodus", it="Iberna",
    pt="Hibernar", nl="Winterslaap", sv="Viloläge", no="Dvale",
    da="Dvale", fi="Horros", ru="Гибернация", ja="休止状態",
    zh="休眠", ko="최대 절전", ar="إسبات")

add("Shut down", fr="Arrêter", de="Herunterfahren", it="Spegni",
    pt="Desligar", nl="Afsluiten", sv="Stäng av", no="Slå av",
    da="Sluk", fi="Sammuta", ru="Выключить", ja="シャットダウン",
    zh="关机", ko="종료", ar="إيقاف التشغيل")

add("File Info", fr="Informations sur le fichier", de="Dateiinformationen",
    it="Informazioni file", pt="Informações do ficheiro", nl="Bestandsinfo",
    sv="Filinformation", no="Filinformasjon", da="Filinfo",
    fi="Tiedoston tiedot", ru="Информация о файле", ja="ファイル情報",
    zh="文件信息", ko="파일 정보", ar="معلومات الملف")

add("Wizard", fr="Assistant", de="Assistent", it="Procedura guidata",
    pt="Assistente", nl="Wizard", sv="Guide", no="Veiviser", da="Guide",
    fi="Ohjattu toiminto", ru="Мастер", ja="ウィザード", zh="向导",
    ko="마법사", ar="معالج")

add("Archive Information", fr="Informations sur l'archive",
    de="Archivinformationen", it="Informazioni archivio",
    pt="Informações do archive", nl="Archiefinformatie",
    sv="Arkivinformation", no="Arkivinformasjon", da="Arkivinformation",
    fi="Arkiston tiedot", ru="Информация об архиве",
    ja="アーカイブ情報", zh="归档信息", ko="아카이브 정보",
    ar="معلومات الأرشيف")

add("English", fr="English", de="English", it="English", pt="English",
    nl="English", sv="English", no="English", da="English", fi="English",
    ru="English", ja="English", zh="English", ko="English", ar="English")

add("Spanish", fr="Español", de="Spanisch", it="Spagnolo", pt="Espanhol",
    nl="Spaans", sv="Spanska", no="Spansk", da="Spansk", fi="Espanja",
    ru="Испанский", ja="スペイン語", zh="西班牙语", ko="스페인어",
    ar="الإسبانية")

# ========== Multi-line strings ========== 

add("0 = Store (fast, large)\n9 = Maximum (slow, small)",
    fr="0 = Stocker (rapide, gros)\n9 = Maximum (lent, petit)",
    de="0 = Speichern (schnell, groß)\n9 = Maximum (langsam, klein)",
    it="0 = Archivia (veloce, grande)\n9 = Massimo (lento, piccolo)",
    pt="0 = Armazenar (rápido, grande)\n9 = Máximo (lento, pequeno)",
    nl="0 = Opslaan (snel, groot)\n9 = Maximaal (langzaam, klein)",
    sv="0 = Lagra (snabb, stor)\n9 = Maximal (långsam, liten)",
    no="0 = Lagre (rask, stor)\n9 = Maksimal (langsom, liten)",
    da="0 = Gem (hurtig, stor)\n9 = Maksimal (langsom, lille)",
    fi="0 = Tallenna (nopea, suuri)\n9 = Maksimi (hidas, pieni)",
    ru="0 = Хранить (быстро, большой)\n9 = Максимум (медленно, маленький)",
    ja="0 = 保存（高速、大）\n9 = 最大（低速、小）",
    zh="0 = 存储（快速，大）\n9 = 最大（慢速，小）",
    ko="0 = 저장 (빠름, 큼)\n9 = 최대 (느림, 작음)",
    ar="0 = تخزين (سريع، كبير)\n9 = أقصى (بطيء، صغير)")

add("ZipFX v1.0\n\nA cross-platform archive manager.\nSupported: ZIP, 7z, RAR, TAR.GZ",
    fr="ZipFX v1.0\n\nGestionnaire d'archives multiplateforme.\nPris en charge : ZIP, 7z, RAR, TAR.GZ",
    de="ZipFX v1.0\n\nEin plattformübergreifender Archiv-Manager.\nUnterstützt: ZIP, 7z, RAR, TAR.GZ",
    it="ZipFX v1.0\n\nUn gestore di archivi multipiattaforma.\nSupporta: ZIP, 7z, RAR, TAR.GZ",
    pt="ZipFX v1.0\n\nUm gestor de arquivos multiplataforma.\nSuporta: ZIP, 7z, RAR, TAR.GZ",
    nl="ZipFX v1.0\n\nEen platformonafhankelijk archiefbeheerprogramma.\nOndersteunt: ZIP, 7z, RAR, TAR.GZ",
    sv="ZipFX v1.0\n\nEn plattformsoberoende arkivhanterare.\nStöder: ZIP, 7z, RAR, TAR.GZ",
    no="ZipFX v1.0\n\nEn plattformsuavhengig arkivbehandler.\nStøtter: ZIP, 7z, RAR, TAR.GZ",
    da="ZipFX v1.0\n\nEn platformsuafhængig arkivhåndterer.\nUnderstøtter: ZIP, 7z, RAR, TAR.GZ",
    fi="ZipFX v1.0\n\nAlustariippumaton arkistonhallintaohjelma.\nTukee: ZIP, 7z, RAR, TAR.GZ",
    ru="ZipFX v1.0\n\nКроссплатформенный менеджер архивов.\nПоддерживает: ZIP, 7z, RAR, TAR.GZ",
    ja="ZipFX v1.0\n\nクロスプラットフォームアーカイブマネージャー。\n対応形式：ZIP, 7z, RAR, TAR.GZ",
    zh="ZipFX v1.0\n\n跨平台归档管理器。\n支持：ZIP, 7z, RAR, TAR.GZ",
    ko="ZipFX v1.0\n\n크로스 플랫폼 아카이브 관리자.\n지원: ZIP, 7z, RAR, TAR.GZ",
    ar="ZipFX v1.0\n\nمدير أرشفة متعدد المنصات.\nيدعم: ZIP, 7z, RAR, TAR.GZ")

add("Supported Archives (*.zip *.7z *.rar *.tar *.tgz *.tar.gz);;ZIP (*.zip);;7z (*.7z);;RAR (*.rar);;TAR (*.tar *.tgz *.tar.gz);;All (*.*)",
    fr="Archives prises en charge (*.zip *.7z *.rar *.tar *.tgz *.tar.gz);;ZIP (*.zip);;7z (*.7z);;RAR (*.rar);;TAR (*.tar *.tgz *.tar.gz);;Tous (*.*)",
    de="Unterstützte Archive (*.zip *.7z *.rar *.tar *.tgz *.tar.gz);;ZIP (*.zip);;7z (*.7z);;RAR (*.rar);;TAR (*.tar *.tgz *.tar.gz);;Alle (*.*)",
    it="Archivi supportati (*.zip *.7z *.rar *.tar *.tgz *.tar.gz);;ZIP (*.zip);;7z (*.7z);;RAR (*.rar);;TAR (*.tar *.tgz *.tar.gz);;Tutti (*.*)",
    pt="Archives suportados (*.zip *.7z *.rar *.tar *.tgz *.tar.gz);;ZIP (*.zip);;7z (*.7z);;RAR (*.rar);;TAR (*.tar *.tgz *.tar.gz);;Todos (*.*)",
    nl="Ondersteunde archieven (*.zip *.7z *.rar *.tar *.tgz *.tar.gz);;ZIP (*.zip);;7z (*.7z);;RAR (*.rar);;TAR (*.tar *.tgz *.tar.gz);;Alle (*.*)",
    sv="Arkiv som stöds (*.zip *.7z *.rar *.tar *.tgz *.tar.gz);;ZIP (*.zip);;7z (*.7z);;RAR (*.rar);;TAR (*.tar *.tgz *.tar.gz);;Alla (*.*)",
    no="Støttede arkiv (*.zip *.7z *.rar *.tar *.tgz *.tar.gz);;ZIP (*.zip);;7z (*.7z);;RAR (*.rar);;TAR (*.tar *.tgz *.tar.gz);;Alle (*.*)",
    da="Understøttede arkiver (*.zip *.7z *.rar *.tar *.tgz *.tar.gz);;ZIP (*.zip);;7z (*.7z);;RAR (*.rar);;TAR (*.tar *.tgz *.tar.gz);;Alle (*.*)",
    fi="Tuetut arkistot (*.zip *.7z *.rar *.tar *.tgz *.tar.gz);;ZIP (*.zip);;7z (*.7z);;RAR (*.rar);;TAR (*.tar *.tgz *.tar.gz);;Kaikki (*.*)",
    ru="Поддерживаемые архивы (*.zip *.7z *.rar *.tar *.tgz *.tar.gz);;ZIP (*.zip);;7z (*.7z);;RAR (*.rar);;TAR (*.tar *.tgz *.tar.gz);;Все (*.*)",
    ja="対応アーカイブ (*.zip *.7z *.rar *.tar *.tgz *.tar.gz);;ZIP (*.zip);;7z (*.7z);;RAR (*.rar);;TAR (*.tar *.tgz *.tar.gz);;すべて (*.*)",
    zh="支持的归档 (*.zip *.7z *.rar *.tar *.tgz *.tar.gz);;ZIP (*.zip);;7z (*.7z);;RAR (*.rar);;TAR (*.tar *.tgz *.tar.gz);;所有 (*.*)",
    ko="지원 아카이브 (*.zip *.7z *.rar *.tar *.tgz *.tar.gz);;ZIP (*.zip);;7z (*.7z);;RAR (*.rar);;TAR (*.tar *.tgz *.tar.gz);;모두 (*.*)",
    ar="الأرشيفات المدعومة (*.zip *.7z *.rar *.tar *.tgz *.tar.gz);;ZIP (*.zip);;7z (*.7z);;RAR (*.rar);;TAR (*.tar *.tgz *.tar.gz);;الكل (*.*)")

add("File exists:\n%1\nOverwrite?",
    fr="Le fichier existe :\n%1\nRemplacer ?",
    de="Datei existiert:\n%1\nÜberschreiben?",
    it="File esistente:\n%1\nSovrascrivere?",
    pt="O ficheiro existe:\n%1\nSobrescrever?",
    nl="Bestand bestaat:\n%1\nOverschrijven?",
    sv="Filen finns:\n%1\nSkriva över?",
    no="Filen finnes:\n%1\nOverskrive?",
    da="Filen findes:\n%1\nOverskriv?",
    fi="Tiedosto on olemassa:\n%1\nKorvataanko?",
    ru="Файл существует:\n%1\nПерезаписать?",
    ja="ファイルが存在します：\n%1\n上書きしますか？",
    zh="文件存在：\n%1\n覆盖？",
    ko="파일이 이미 있습니다:\n%1\n덮어쓰시겠습니까?",
    ar="الملف موجود:\n%1\nكتابة فوق؟")

add("File: %1\nSize: %2 bytes",
    fr="Fichier : %1\nTaille : %2 octets",
    de="Datei: %1\nGröße: %2 Bytes",
    it="File: %1\nDimensione: %2 byte",
    pt="Ficheiro: %1\nTamanho: %2 bytes",
    nl="Bestand: %1\nGrootte: %2 bytes",
    sv="Fil: %1\nStorlek: %2 byte",
    no="Fil: %1\nStørrelse: %2 bytes",
    da="Fil: %1\nStørrelse: %2 bytes",
    fi="Tiedosto: %1\nKoko: %2 tavua",
    ru="Файл: %1\nРазмер: %2 байт",
    ja="ファイル: %1\nサイズ: %2 バイト",
    zh="文件：%1\n大小：%2 字节",
    ko="파일: %1\n크기: %2 바이트",
    ar="الملف: %1\nالحجم: %2 بايت")

add("%1  —  %2 bytes",
    fr="%1  —  %2 octets", de="%1  —  %2 Bytes", it="%1  —  %2 byte",
    pt="%1  —  %2 bytes", nl="%1  —  %2 bytes", sv="%1  —  %2 byte",
    no="%1  —  %2 bytes", da="%1  —  %2 bytes", fi="%1  —  %2 tavua",
    ru="%1  —  %2 байт", ja="%1  —  %2 バイト", zh="%1  —  %2 字节",
    ko="%1  —  %2 바이트", ar="%1  —  %2 بايت")

add("File: %1\nSize: %2 bytes\n\nCannot preview this format.",
    fr="Fichier : %1\nTaille : %2 octets\n\nImpossible de prévisualiser ce format.",
    de="Datei: %1\nGröße: %2 Bytes\n\nVorschau für dieses Format nicht möglich.",
    it="File: %1\nDimensione: %2 byte\n\nImpossibile visualizzare l'anteprima di questo formato.",
    pt="Ficheiro: %1\nTamanho: %2 bytes\n\nNão é possível pré-visualizar este formato.",
    nl="Bestand: %1\nGrootte: %2 bytes\n\nKan dit formaat niet voorvertonen.",
    sv="Fil: %1\nStorlek: %2 byte\n\nKan inte förhandsgranska detta format.",
    no="Fil: %1\nStørrelse: %2 bytes\n\nKan ikke forhåndsvise dette formatet.",
    da="Fil: %1\nStørrelse: %2 bytes\n\nKan ikke forhåndsvise dette format.",
    fi="Tiedosto: %1\nKoko: %2 tavua\n\nTätä muotoa ei voi esikatsella.",
    ru="Файл: %1\nРазмер: %2 байт\n\nНевозможно просмотреть этот формат.",
    ja="ファイル: %1\nサイズ: %2 バイト\n\nこの形式はプレビューできません。",
    zh="文件：%1\n大小：%2 字节\n\n无法预览此格式。",
    ko="파일: %1\n크기: %2 바이트\n\n이 형식은 미리 볼 수 없습니다.",
    ar="الملف: %1\nالحجم: %2 بايت\n\nلا يمكن معاينة هذا التنسيق.")

add("Failed to load image.",
    fr="Échec du chargement de l'image.", de="Bild konnte nicht geladen werden.",
    it="Caricamento immagine fallito.", pt="Falha ao carregar a imagem.",
    nl="Afbeelding laden mislukt.", sv="Kunde inte ladda bilden.",
    no="Kunne ikke laste bildet.", da="Kunne ikke indlæse billedet.",
    fi="Kuvan lataus epäonnistui.", ru="Не удалось загрузить изображение.",
    ja="画像の読み込みに失敗しました。", zh="加载图像失败。",
    ko="이미지를 불러오지 못했습니다.", ar="فشل تحميل الصورة.")

add("\n... truncated (showing first 100000 bytes)",
    fr="\n... tronqué (affichage des 100000 premiers octets)",
    de="\n... abgeschnitten (erste 100000 Bytes werden angezeigt)",
    it="\n... troncato (mostra primi 100000 byte)",
    pt="\n... truncado (a mostrar primeiros 100000 bytes)",
    nl="\n... afgekapt (eerste 100000 bytes worden getoond)",
    sv="\n... trunkerad (visar första 100000 byte)",
    no="\n... forkortet (viser første 100000 bytes)",
    da="\n... afkortet (viser første 100000 bytes)",
    fi="\n... katkaistu (näytetään ensimmäiset 100000 tavua)",
    ru="\n... обрезано (показаны первые 100000 байт)",
    ja="\n... 省略（最初の100000バイトを表示）",
    zh="\n... 已截断（显示前 100000 个字节）",
    ko="\n... 잘림 (처음 100000바이트 표시)",
    ar="\n... مقتطع (إظهار أول 100000 بايت)")

add("... truncated (showing first 4096 bytes)",
    fr="... tronqué (affichage des 4096 premiers octets)",
    de="... abgeschnitten (erste 4096 Bytes werden angezeigt)",
    it="... troncato (mostra primi 4096 byte)",
    pt="... truncado (a mostrar primeiros 4096 bytes)",
    nl="... afgekapt (eerste 4096 bytes worden getoond)",
    sv="... trunkerad (visar första 4096 byte)",
    no="... forkortet (viser første 4096 bytes)",
    da="... afkortet (viser første 4096 bytes)",
    fi="... katkaistu (näytetään ensimmäiset 4096 tavua)",
    ru="... обрезано (показаны первые 4096 байт)",
    ja="... 省略（最初の4096バイトを表示）",
    zh="... 已截断（显示前 4096 个字节）",
    ko="... 잘림 (처음 4096바이트 표시)",
    ar="... مقتطع (إظهار أول 4096 بايت)")

add("Archive: %1\n",
    fr="Archive : %1\n", de="Archiv: %1\n", it="Archivio: %1\n",
    pt="Archivo: %1\n", nl="Archief: %1\n", sv="Arkiv: %1\n",
    no="Arkiv: %1\n", da="Arkiv: %1\n", fi="Arkisto: %1\n",
    ru="Архив: %1\n", ja="アーカイブ: %1\n", zh="归档：%1\n",
    ko="아카이브: %1\n", ar="الأرشيف: %1\n")

add("Format: %1\n",
    fr="Format : %1\n", de="Format: %1\n", it="Formato: %1\n",
    pt="Formato: %1\n", nl="Formaat: %1\n", sv="Format: %1\n",
    no="Format: %1\n", da="Format: %1\n", fi="Muoto: %1\n",
    ru="Формат: %1\n", ja="形式: %1\n", zh="格式：%1\n",
    ko="형식: %1\n", ar="التنسيق: %1\n")

add("Files: %1\n",
    fr="Fichiers : %1\n", de="Dateien: %1\n", it="File: %1\n",
    pt="Ficheiros: %1\n", nl="Bestanden: %1\n", sv="Filer: %1\n",
    no="Filer: %1\n", da="Filer: %1\n", fi="Tiedostot: %1\n",
    ru="Файлы: %1\n", ja="ファイル: %1\n", zh="文件：%1\n",
    ko="파일: %1\n", ar="الملفات: %1\n")

add("Folders: %1\n",
    fr="Dossiers : %1\n", de="Ordner: %1\n", it="Cartelle: %1\n",
    pt="Pastas: %1\n", nl="Mappen: %1\n", sv="Mappar: %1\n",
    no="Mapper: %1\n", da="Mapper: %1\n", fi="Kansiot: %1\n",
    ru="Папки: %1\n", ja="フォルダ: %1\n", zh="文件夹：%1\n",
    ko="폴더: %1\n", ar="المجلدات: %1\n")

add("Total size: %1 bytes\n",
    fr="Taille totale : %1 octets\n", de="Gesamtgröße: %1 Bytes\n",
    it="Dimensione totale: %1 byte\n", pt="Tamanho total: %1 bytes\n",
    nl="Totale grootte: %1 bytes\n", sv="Total storlek: %1 byte\n",
    no="Total størrelse: %1 bytes\n", da="Total størrelse: %1 bytes\n",
    fi="Kokonaiskoko: %1 tavua\n", ru="Общий размер: %1 байт\n",
    ja="合計サイズ: %1 バイト\n", zh="总大小：%1 字节\n",
    ko="전체 크기: %1 바이트\n", ar="الحجم الإجمالي: %1 بايت\n")

add("Packed size: %1 bytes\n",
    fr="Taille compressée : %1 octets\n", de="Komprimierte Größe: %1 Bytes\n",
    it="Dimensione compressa: %1 byte\n", pt="Tamanho comprimido: %1 bytes\n",
    nl="Gecomprimeerde grootte: %1 bytes\n", sv="Packad storlek: %1 byte\n",
    no="Pakket størrelse: %1 bytes\n", da="Pakket størrelse: %1 bytes\n",
    fi="Pakattu koko: %1 tavua\n", ru="Сжатый размер: %1 байт\n",
    ja="圧縮サイズ: %1 バイト\n", zh="压缩后大小：%1 字节\n",
    ko="압축 크기: %1 바이트\n", ar="الحجم المضغوط: %1 بايت\n")

add("Compression ratio: %1%\n",
    fr="Taux de compression : %1%\n", de="Komprimierungsverhältnis: %1%\n",
    it="Rapporto di compressione: %1%\n", pt="Taxa de compressão: %1%\n",
    nl="Compressieverhouding: %1%\n", sv="Kompressionsförhållande: %1%\n",
    no="Komprimeringsforhold: %1%\n", da="Kompressionsforhold: %1%\n",
    fi="Pakkaussuhde: %1%\n", ru="Степень сжатия: %1%\n",
    ja="圧縮率: %1%\n", zh="压缩率：%1%\n", ko="압축 비율: %1%\n",
    ar="نسبة الضغط: %1%\n")

add("Language changed to %1.\nRestart the app for the change to take full effect.",
    fr="Langue changée en %1.\nRedémarrez l'application pour que le changement prenne plein effet.",
    de="Sprache geändert auf %1.\nStarten Sie die App neu, damit die Änderung vollständig wirksam wird.",
    it="Lingua cambiata in %1.\nRiavviare l'app per applicare completamente la modifica.",
    pt="Idioma alterado para %1.\nReinicie a aplicação para que a alteração tenha efeito total.",
    nl="Taal gewijzigd naar %1.\nHerstart de app om de wijziging volledig door te voeren.",
    sv="Språk ändrat till %1.\nStarta om appen för att ändringen ska träda i full kraft.",
    no="Språk endret til %1.\nStart appen på nytt for at endringen skal tre i kraft.",
    da="Sprog ændret til %1.\nGenstart appen for at ændringen træder fuldt i kraft.",
    fi="Kieli vaihdettu: %1.\nKäynnista sovellus uudelleen, jotta muutos tulee täysin voimaan.",
    ru="Язык изменён на %1.\nПерезапустите приложение, чтобы изменение вступило в полную силу.",
    ja="言語を %1 に変更しました。\n変更を完全に有効にするには、アプリを再起動してください。",
    zh="语言已更改为 %1。\n重新启动应用程序以使更改完全生效。",
    ko="언어가 %1(으)로 변경되었습니다.\n변경 사항을 완전히 적용하려면 앱을 다시 시작하십시오.",
    ar="تم تغيير اللغة إلى %1.\nأعد تشغيل التطبيق حتى يصبح التغيير ساري المفعول بالكامل.")

add("ZipFX v%1\n\nMultiplatform archiver for power users.\nSupported: ZIP, 7z, RAR, TAR.GZ, ISO, CAB, LHA, XAR, CPIO, ...",
    fr="ZipFX v%1\n\nArchiveur multiplateforme pour utilisateurs avancés.\nPris en charge : ZIP, 7z, RAR, TAR.GZ, ISO, CAB, LHA, XAR, CPIO, ...",
    de="ZipFX v%1\n\nPlattformübergreifender Archivierer für erfahrene Benutzer.\nUnterstützt: ZIP, 7z, RAR, TAR.GZ, ISO, CAB, LHA, XAR, CPIO, ...",
    it="ZipFX v%1\n\nArchiviatore multipiattaforma per utenti esperti.\nSupporta: ZIP, 7z, RAR, TAR.GZ, ISO, CAB, LHA, XAR, CPIO, ...",
    pt="ZipFX v%1\n\nArquivador multiplataforma para utilizadores avançados.\nSuporta: ZIP, 7z, RAR, TAR.GZ, ISO, CAB, LHA, XAR, CPIO, ...",
    nl="ZipFX v%1\n\nMultiplatform archiver voor gevorderde gebruikers.\nOndersteunt: ZIP, 7z, RAR, TAR.GZ, ISO, CAB, LHA, XAR, CPIO, ...",
    sv="ZipFX v%1\n\nPlattformsoberoende arkiverare för avancerade användare.\nStöder: ZIP, 7z, RAR, TAR.GZ, ISO, CAB, LHA, XAR, CPIO, ...",
    no="ZipFX v%1\n\nMultiplatform-arkiver for avanserte brukere.\nStøtter: ZIP, 7z, RAR, TAR.GZ, ISO, CAB, LHA, XAR, CPIO, ...",
    da="ZipFX v%1\n\nMultiplatform-arkiver til erfarne brugere.\nUnderstøtter: ZIP, 7z, RAR, TAR.GZ, ISO, CAB, LHA, XAR, CPIO, ...",
    fi="ZipFX v%1\n\nMonialustainen arkistointiohjelma tehokäyttäjille.\nTukee: ZIP, 7z, RAR, TAR.GZ, ISO, CAB, LHA, XAR, CPIO, ...",
    ru="ZipFX v%1\n\nМногоплатформенный архиватор для опытных пользователей.\nПоддерживает: ZIP, 7z, RAR, TAR.GZ, ISO, CAB, LHA, XAR, CPIO, ...",
    ja="ZipFX v%1\n\nパワーユーザー向けのクロスプラットフォームアーカイバ。\n対応形式：ZIP, 7z, RAR, TAR.GZ, ISO, CAB, LHA, XAR, CPIO, ...",
    zh="ZipFX v%1\n\n面向高级用户的多平台存档器。\n支持：ZIP, 7z, RAR, TAR.GZ, ISO, CAB, LHA, XAR, CPIO, ...",
    ko="ZipFX v%1\n\n고급 사용자를 위한 멀티플랫폼 아카이버.\n지원: ZIP, 7z, RAR, TAR.GZ, ISO, CAB, LHA, XAR, CPIO, ...",
    ar="ZipFX v%1\n\nأداة أرشفة متعددة المنصات للمستخدمين المحترفين.\nيدعم: ZIP, 7z, RAR, TAR.GZ, ISO, CAB, LHA, XAR, CPIO, ...")

# ========== Menu items with &amp; ==========

add("&amp;File",
    fr="&amp;Fichier", de="&amp;Datei", it="&amp;File", pt="&amp;Ficheiro",
    nl="&amp;Bestand", sv="&amp;Arkiv", no="&amp;Arkiv", da="&amp;Arkiv",
    fi="&amp;Tiedosto", ru="&amp;Файл", ja="&amp;ファイル", zh="&amp;文件",
    ko="&amp;파일", ar="&amp;ملف")

add("&amp;New Archive...\tCtrl+N",
    fr="&amp;Nouvelle archive...\tCtrl+N", de="&amp;Neues Archiv...\tCtrl+N",
    it="&amp;Nuovo archivio...\tCtrl+N", pt="&amp;Nova archive...\tCtrl+N",
    nl="&amp;Nieuw archief...\tCtrl+N", sv="&amp;Nytt arkiv...\tCtrl+N",
    no="&amp;Nytt arkiv...\tCtrl+N", da="&amp;Nyt arkiv...\tCtrl+N",
    fi="&amp;Uusi arkisto...\tCtrl+N", ru="&amp;Новый архив...\tCtrl+N",
    ja="&amp;新しいアーカイブ...\tCtrl+N", zh="&amp;新建归档...\tCtrl+N",
    ko="&amp;새 아카이브...\tCtrl+N", ar="&amp;أرشيف جديد...\tCtrl+N")

add("&amp;Open Archive...\tCtrl+O",
    fr="&amp;Ouvrir l'archive...\tCtrl+O", de="&amp;Archiv öffnen...\tCtrl+O",
    it="&amp;Apri archivio...\tCtrl+O", pt="&amp;Abrir archive...\tCtrl+O",
    nl="&amp;Archief openen...\tCtrl+O", sv="&amp;Öppna arkiv...\tCtrl+O",
    no="&amp;Åpne arkiv...\tCtrl+O", da="&amp;Åbn arkiv...\tCtrl+O",
    fi="&amp;Avaa arkisto...\tCtrl+O", ru="&amp;Открыть архив...\tCtrl+O",
    ja="&amp;アーカイブを開く...\tCtrl+O", zh="&amp;打开归档...\tCtrl+O",
    ko="&amp;아카이브 열기...\tCtrl+O", ar="&amp;فتح أرشيف...\tCtrl+O")

add("&amp;Close Archive\tCtrl+C",
    fr="&amp;Fermer l'archive\tCtrl+C", de="&amp;Archiv schließen\tCtrl+C",
    it="&amp;Chiudi archivio\tCtrl+C", pt="&amp;Fechar archive\tCtrl+C",
    nl="&amp;Archief sluiten\tCtrl+C", sv="&amp;Stäng arkiv\tCtrl+C",
    no="&amp;Lukk arkiv\tCtrl+C", da="&amp;Luk arkiv\tCtrl+C",
    fi="&amp;Sulje arkisto\tCtrl+C", ru="&amp;Закрыть архив\tCtrl+C",
    ja="&amp;アーカイブを閉じる\tCtrl+C", zh="&amp;关闭归档\tCtrl+C",
    ko="&amp;아카이브 닫기\tCtrl+C", ar="&amp;إغلاق الأرشيف\tCtrl+C")

add("E&amp;xit\tAlt+F4",
    fr="E&amp;xit\tAlt+F4", de="E&amp;xit\tAlt+F4", it="E&amp;sci\tAlt+F4",
    pt="&amp;Sair\tAlt+F4", nl="A&amp;fsluiten\tAlt+F4", sv="A&amp;vsluta\tAlt+F4",
    no="A&amp;vslutt\tAlt+F4", da="A&amp;fslut\tAlt+F4", fi="L&amp;opeta\tAlt+F4",
    ru="В&amp;ыход\tAlt+F4", ja="終&amp;了\tAlt+F4", zh="退&amp;出\tAlt+F4",
    ko="종&amp;료\tAlt+F4", ar="&amp;خروج\tAlt+F4")

add("&amp;Commands",
    fr="&amp;Commandes", de="&amp;Befehle", it="&amp;Comandi", pt="&amp;Comandos",
    nl="&amp;Opdrachten", sv="&amp;Kommandon", no="&amp;Kommandoer",
    da="&amp;Kommandoer", fi="&amp;Komennot", ru="&amp;Команды",
    ja="&amp;コマンド", zh="&amp;命令", ko="&amp;명령", ar="&amp;أوامر")

add("&amp;Add Files...\tAlt+A",
    fr="&amp;Ajouter des fichiers...\tAlt+A", de="&amp;Dateien hinzufügen...\tAlt+A",
    it="&amp;Aggiungi file...\tAlt+A", pt="&amp;Adicionar ficheiros...\tAlt+A",
    nl="&amp;Bestanden toevoegen...\tAlt+A", sv="&amp;Lägg till filer...\tAlt+A",
    no="&amp;Legg til filer...\tAlt+A", da="&amp;Tilføj filer...\tAlt+A",
    fi="&amp;Lisää tiedostoja...\tAlt+A", ru="&amp;Добавить файлы...\tAlt+A",
    ja="&amp;ファイルを追加...\tAlt+A", zh="&amp;添加文件...\tAlt+A",
    ko="&amp;파일 추가...\tAlt+A", ar="&amp;إضافة ملفات...\tAlt+A")

add("E&amp;xtract...\tAlt+E",
    fr="E&amp;xtraire...\tAlt+E", de="E&amp;xtrahieren...\tAlt+E",
    it="E&amp;strai...\tAlt+E", pt="E&amp;xtrair...\tAlt+E",
    nl="U&amp;itpakken...\tAlt+E", sv="E&amp;xtrahera...\tAlt+E",
    no="&amp;Pakk ut...\tAlt+E", da="&amp;Udpak...\tAlt+E",
    fi="&amp;Pura...\tAlt+E", ru="&amp;Извлечь...\tAlt+E",
    ja="&amp;展開...\tAlt+E", zh="&amp;提取...\tAlt+E",
    ko="&amp;추출...\tAlt+E", ar="&amp;استخراج...\tAlt+E")

add("&amp;Test\tAlt+T",
    fr="&amp;Tester\tAlt+T", de="&amp;Prüfen\tAlt+T", it="&amp;Test\tAlt+T",
    pt="&amp;Testar\tAlt+T", nl="&amp;Testen\tAlt+T", sv="&amp;Testa\tAlt+T",
    no="&amp;Test\tAlt+T", da="&amp;Test\tAlt+T", fi="&amp;Testaa\tAlt+T",
    ru="&amp;Тест\tAlt+T", ja="&amp;テスト\tAlt+T", zh="&amp;测试\tAlt+T",
    ko="&amp;테스트\tAlt+T", ar="&amp;اختبار\tAlt+T")

add("&amp;View\tAlt+V",
    fr="&amp;Voir\tAlt+V", de="&amp;Ansicht\tAlt+V", it="&amp;Visualizza\tAlt+V",
    pt="&amp;Ver\tAlt+V", nl="&amp;Weergave\tAlt+V", sv="&amp;Visa\tAlt+V",
    no="&amp;Vis\tAlt+V", da="&amp;Vis\tAlt+V", fi="&amp;Näytä\tAlt+V",
    ru="&amp;Просмотр\tAlt+V", ja="&amp;表示\tAlt+V", zh="&amp;查看\tAlt+V",
    ko="&amp;보기\tAlt+V", ar="&amp;عرض\tAlt+V")

add("&amp;Delete\tDel",
    fr="&amp;Supprimer\tSuppr", de="&amp;Löschen\tEntf",
    it="&amp;Elimina\tCanc", pt="&amp;Eliminar\tDel",
    nl="&amp;Verwijderen\tDel", sv="&amp;Ta bort\tDel",
    no="&amp;Slett\tDel", da="&amp;Slet\tDel", fi="&amp;Poista\tDel",
    ru="&amp;Удалить\tDel", ja="&amp;削除\tDel", zh="&amp;删除\tDel",
    ko="&amp;삭제\tDel", ar="&amp;حذف\tDel")

add("&amp;Find...\tF3",
    fr="&amp;Rechercher...\tF3", de="&amp;Suchen...\tF3", it="&amp;Cerca...\tF3",
    pt="&amp;Procurar...\tF3", nl="&amp;Zoeken...\tF3", sv="&amp;Sök...\tF3",
    no="&amp;Søk...\tF3", da="&amp;Søg...\tF3", fi="&amp;Etsi...\tF3",
    ru="&amp;Найти...\tF3", ja="&amp;検索...\tF3", zh="&amp;查找...\tF3",
    ko="&amp;찾기...\tF3", ar="&amp;بحث...\tF3")

add("&amp;Wizard...\tCtrl+W",
    fr="&amp;Assistant...\tCtrl+W", de="&amp;Assistent...\tCtrl+W",
    it="&amp;Procedura guidata...\tCtrl+W", pt="&amp;Assistente...\tCtrl+W",
    nl="&amp;Wizard...\tCtrl+W", sv="&amp;Guide...\tCtrl+W",
    no="&amp;Veiviser...\tCtrl+W", da="&amp;Guide...\tCtrl+W",
    fi="&amp;Ohjattu toiminto...\tCtrl+W", ru="&amp;Мастер...\tCtrl+W",
    ja="&amp;ウィザード...\tCtrl+W", zh="&amp;向导...\tCtrl+W",
    ko="&amp;마법사...\tCtrl+W", ar="&amp;معالج...\tCtrl+W")

add("&amp;Information...\tCtrl+I",
    fr="&amp;Informations...\tCtrl+I", de="&amp;Informationen...\tCtrl+I",
    it="&amp;Informazioni...\tCtrl+I", pt="&amp;Informações...\tCtrl+I",
    nl="&amp;Informatie...\tCtrl+I", sv="&amp;Information...\tCtrl+I",
    no="&amp;Informasjon...\tCtrl+I", da="&amp;Information...\tCtrl+I",
    fi="&amp;Tiedot...\tCtrl+I", ru="&amp;Информация...\tCtrl+I",
    ja="&amp;情報...\tCtrl+I", zh="&amp;信息...\tCtrl+I",
    ko="&amp;정보...\tCtrl+I", ar="&amp;معلومات...\tCtrl+I")

add("&amp;Options",
    fr="&amp;Options", de="&amp;Optionen", it="&amp;Opzioni", pt="&amp;Opções",
    nl="&amp;Opties", sv="&amp;Alternativ", no="&amp;Alternativer",
    da="&amp;Indstillinger", fi="&amp;Asetukset", ru="&amp;Параметры",
    ja="&amp;オプション", zh="&amp;选项", ko="&amp;옵션", ar="&amp;خيارات")

add("&amp;Flat File List",
    fr="&amp;Liste plate", de="&amp;Flache Dateiliste", it="&amp;Elenco piatto",
    pt="&amp;Lista plana", nl="&amp;Platte lijst", sv="&amp;Platt filista",
    no="&amp;Flat filliste", da="&amp;Flad filliste",
    fi="&amp;Tasoinen tiedostoluettelo", ru="&amp;Плоский список файлов",
    ja="&amp;フラットファイルリスト", zh="&amp;平面文件列表",
    ko="&amp;플랫 파일 목록", ar="&amp;قائمة ملفات مسطحة")

add("&amp;Help",
    fr="&amp;Aide", de="&amp;Hilfe", it="&amp;Aiuto", pt="&amp;Ajuda",
    nl="&amp;Help", sv="&amp;Hjälp", no="&amp;Hjelp", da="&amp;Hjælp",
    fi="&amp;Ohje", ru="&amp;Помощь", ja="&amp;ヘルプ", zh="&amp;帮助",
    ko="&amp;도움말", ar="&amp;مساعدة")

add("About ZipFX",
    fr="À propos de ZipFX", de="Über ZipFX",
    it="Informazioni su ZipFX", pt="Sobre o ZipFX",
    nl="Over ZipFX", sv="Om ZipFX", no="Om ZipFX",
    da="Om ZipFX", fi="Tietoja ZipFX:stä", ru="О ZipFX",
    ja="ZipFXについて", zh="关于 ZipFX", ko="ZipFX 정보", ar="حول ZipFX")

add("&amp;About ZipFX",
    fr="&amp;À propos de ZipFX", de="&amp;Über ZipFX",
    it="&amp;Informazioni su ZipFX", pt="&amp;Sobre o ZipFX",
    nl="&amp;Over ZipFX", sv="&amp;Om ZipFX", no="&amp;Om ZipFX",
    da="&amp;Om ZipFX", fi="&amp;Tietoja ZipFX:stä", ru="&amp;О ZipFX",
    ja="&amp;ZipFXについて", zh="&amp;关于 ZipFX", ko="&amp;ZipFX 정보",
    ar="&amp;حول ZipFX")

add("&amp;Language",
    fr="&amp;Langue", de="&amp;Sprache", it="&amp;Lingua", pt="&amp;Idioma",
    nl="&amp;Taal", sv="&amp;Språk", no="&amp;Språk", da="&amp;Sprog",
    fi="&amp;Kieli", ru="&amp;Язык", ja="&amp;言語", zh="&amp;语言",
    ko="&amp;언어", ar="&amp;اللغة")

add("Add Fol&amp;der...\tAlt+D",
    fr="Ajouter un &amp;dossier...\tAlt+D", de="&amp;Ordner hinzufügen...\tAlt+D",
    it="Aggiungi &amp;cartella...\tAlt+D", pt="Adicionar &amp;pasta...\tAlt+D",
    nl="&amp;Map toevoegen...\tAlt+D", sv="Lägg till &amp;mapp...\tAlt+D",
    no="Legg til &amp;mappe...\tAlt+D", da="Tilføj &amp;mappe...\tAlt+D",
    fi="Lisää &amp;kansio...\tAlt+D", ru="Добавить &amp;папку...\tAlt+D",
    ja="&amp;フォルダを追加...\tAlt+D", zh="添加&amp;文件夹...\tAlt+D",
    ko="&amp;폴더 추가...\tAlt+D", ar="إضافة &amp;مجلد...\tAlt+D")


# ============================================================
# File processing
# ============================================================

def process_ts_file(lang_code, qt_code, lang_name):
    """Create .ts file for a given language based on English template."""
    en_content = open(EN_FILE, 'r', encoding='utf-8').read()
    ts_file = TRANSLATIONS_DIR / f"zipfx_{lang_code}.ts"

    # Replace language attribute
    content = en_content.replace('language="en_US"', f'language="{qt_code}"')

    # Parse: find all <message> blocks and replace translations
    # Use regex to match message blocks with multiline source support
    message_pattern = re.compile(
        r'(?ms)(<message>\s*(?:<location[^>]*/>\s*)*<source>)(.*?)(</source>\s*)<translation[^>]*>(.*?)(</translation>)(\s*</message>)'
    )

    translated_count = 0
    untranslated_count = 0

    def replace_translation(match):
        nonlocal translated_count, untranslated_count
        src_text = match.group(2)
        prefix = match.group(1)
        middle = match.group(3)
        trans_close = match.group(5)
        rest = match.group(6)

        if src_text in T and lang_code in T[src_text]:
            trans_value = T[src_text][lang_code]
            # Escape XML special chars
            trans_value = trans_value.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')
            translated_count += 1
            return f'{prefix}{src_text}{middle}<translation>{trans_value}{trans_close}{rest}'
        else:
            untranslated_count += 1
            print(f"  [WARN] No translation for: {src_text!r}")
            return f'{prefix}{src_text}{middle}<translation type="unfinished"></translation>{rest}'

    new_content = message_pattern.sub(replace_translation, content)

    # Write the file
    with open(ts_file, 'w', encoding='utf-8', newline='\n') as f:
        f.write(new_content)

    return translated_count, untranslated_count


# Main
print(f"Loaded {len(T)} translation entries.")
print(f"Processing {len(LANGUAGES)} languages...")
print()

results = []
for lang_code, qt_code, lang_name in LANGUAGES:
    print(f"Processing {lang_code} ({lang_name})...")
    translated, untranslated = process_ts_file(lang_code, qt_code, lang_name)
    results.append((lang_name, lang_code, translated, untranslated))
    print(f"  Translated: {translated}  Unfinished: {untranslated}")

print()
print("=" * 60)
print("SUMMARY")
print("=" * 60)
for name, code, t, u in results:
    fn = f"zipfx_{code}.ts"
    print(f"  {name:12s} ({code:2s}): {t:3d} translated, {u:3d} unfinished  -> {fn}")
print()
print("Done.")

# Save also to JSON for reference
json_out = TRANSLATIONS_DIR / ".." / "translations_report.json"
with open(json_out, 'w', encoding='utf-8') as f:
    json.dump(results, f, ensure_ascii=False, indent=2)
