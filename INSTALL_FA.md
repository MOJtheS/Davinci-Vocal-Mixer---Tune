# نصب و استفاده از HumRig در DaVinci Resolve (مک)

HumRig یک پلاگین صوتی (AU و VST3) برای Fairlight است. روی صدای خام زمزمه/آواز می‌گذاری و
**Auto-tune، EQ، کمپرسور، De-Esser، Drive، Reverb و Delay** را یک‌جا می‌گیری.

## ۱) نصب (ساده‌ترین راه)

1. فایل `HumRig-macOS.zip` را از صفحه‌ی **Releases** ریپوی GitHub دانلود کن و باز کن (Unzip). (اگر از بخش Actions → Artifacts دانلود کردی، یک بار دیگر هم باید zip داخلش را باز کنی.)
2. داخل پوشه روی **`install-mac.command`** دابل‌کلیک کن.
   - اگر مک گفت «از منبع نامعتبر است»: روی فایل **راست‌کلیک ← Open ← Open** را بزن.
   - در macOS 15 به بعد این کار گاهی جواب نمی‌دهد: **System Settings ← Privacy & Security** را باز کن، پایین صفحه کنار نام فایل **Open Anyway** را بزن.
   - اگر باز هم نشد، از «نصب دستی» پایین (Terminal) استفاده کن؛ همیشه کار می‌کند.
3. وقتی نوشت «HumRig installed»، یک کلید بزن تا پنجره بسته شود.
4. **داوینچی را کاملاً ببند (⌘Q) و دوباره باز کن.**

### نصب دستی (اگر اسکریپت کار نکرد)
در Terminal، داخل پوشه‌ی unzip‌شده این‌ها را بزن:

```bash
cp -R Components/HumRig.component ~/Library/Audio/Plug-Ins/Components/
cp -R VST3/HumRig.vst3 ~/Library/Audio/Plug-Ins/VST3/
xattr -cr ~/Library/Audio/Plug-Ins/Components/HumRig.component ~/Library/Audio/Plug-Ins/VST3/HumRig.vst3
```

> دستور `xattr -cr` پرچم «دانلودشده از اینترنت» را برمی‌دارد؛ بدون آن مک پلاگین را بلاک می‌کند.

## ۲) فعال‌سازی در داوینچی

1. داوینچی را باز کن: **DaVinci Resolve ← Preferences ← System ← Audio Plugins**.
2. اگر HumRig در لیست نبود دکمه‌ی **Refresh / Rescan** را بزن.
3. کنار **HumRig** تیک بگذار (فعال) و Save کن.

## ۳) استفاده

1. به صفحه‌ی **Fairlight** برو.
2. در Mixer، روی تراک زمزمه‌ات، قسمت **Effects** را دابل‌کلیک کن ← **Audio Units** (یا **VST3**) ← **HumRig**.
3. پنجره‌ی HumRig باز می‌شود. ترتیب پیشنهادی:
   - **Male / Female** را مطابق صدا انتخاب کن.
   - در بخش **Pitch Correction** کلید و اسکیل آهنگ را بزن (مثلاً `C` و `Major`). اگر کلید را نمی‌دانی `Chromatic` بگذار.
   - **Speed**: ۱۰۰ = حالت «Auto-tune رباتی»، کمتر = طبیعی‌تر.
   - **Flex**: بالاتر = نوسان طبیعی کوچک صدا دست‌نخورده می‌ماند.
   - یک **Preset** انتخاب کن (Pop، Ballad، K-Pop، Hum Cleanup …) و بعد با knobها دقیق‌ترش کن.
   - **Dry/Wet** را برای مقایسه با صدای اصلی استفاده کن.

### نکات مهم
- **قبل از HumRig، حذف نویز بگذار.** صدای خام موبایل نویز دارد و auto-tune روی نویز بد کار می‌کند.
  در Resolve Studio: روی همان تراک **Voice Isolation** و/یا **Noise Reduction** را *قبل* از HumRig در لیست Effects قرار بده.
- پلاگین حدود ۱۲۰ میلی‌ثانیه تأخیر دارد و به داوینچی اعلام می‌کند تا جبران کند. **این را باید روی خود تو تست کنیم:** اگر صدا نسبت به تصویر دیرتر شنیده شد، بگو؛ نسخه‌ی کم‌تأخیر می‌سازم.
- روی تراک **Mono** هم کار می‌کند (بهترین حالت برای زمزمه).
- اگر نت تشخیص داده نشد، نمایشگر مشکی بالای knobها «no pitch» نشان می‌دهد؛ **Range** را عوض کن یا Input را بالا ببر.

## ۳.۱) قابلیت‌های نسخهٔ ۰.۲

### دکمهٔ Listening (بالای پنجره، صورتی)
1. فقط **همان تراک وکال** را پلی کن (بقیهٔ HumRig‌ها را سولو/بی‌صدا کن).
2. روی **Listening** کلیک کن. درصد پیشرفت نشان داده می‌شود (حدود ۶ ثانیه صدای خوانده‌شده لازم است).
3. وقتی تمام شد پلاگین خودش این‌ها را تنظیم می‌کند: **Key و Scale** (Major/Minor)، **Male/Female و Range**،
   **Input و Comp**، **Warmth / Presence / Air** و **De-Esser**. نتیجه روی دکمه نوشته می‌شود (مثلاً «A minor | male»).
4. تنظیمات یک نقطهٔ شروع هوشمندانه است (روش ابتکاری)؛ بعدش با گوش خودت دقیق‌ترش کن. دوباره کلیک = تحلیل جدید؛ وسط کار کلیک = لغو.

### نشانگرهای زنده (Realtime)
GR meter، نوارهای INPUT/OUTPUT و نمایشگر نت حالا حتی در صفحهٔ **Edit/Cut** هم هنگام پخش حرکت می‌کنند
(داوینچی پنجرهٔ پلاگین را روی یک نمونه و صدا را روی نمونهٔ دیگری پردازش می‌کند؛ پلاگین این دو را وصل می‌کند).
اگر چند HumRig همزمان پلی می‌شوند، نشانگرها آخرین نمونهٔ فعال را نشان می‌دهند.

### ۲۵ پریست خواننده‌ها (منوی Preset، به تفکیک ژانر)
بر اساس تحقیق عمیق دربارهٔ زنجیرهٔ وکال هنرمندان Billboard (گزارش کامل: `docs/vocal-mix-research-fa.md`). **جهت** تنظیمات (خشک/خیس، فشرده/نرم،
تیون سخت/بدون تیون) از منابع مستند است؛ **عددهای دقیق دستگیره‌ها تفسیر ما هستند**، نه اندازه‌گیری واقعی.

| ژانر | پریست‌ها |
|---|---|
| Pop | Olivia Rodrigo، Billie Eilish، Taylor Swift، Ariana Grande، The Weeknd (کم‌مستند) |
| Rap | Future، Lil Baby، Kendrick Lamar، 21 Savage، Eminem |
| Rock | Panic! At The Disco، Paramore، Foo Fighters، Twenty One Pilots (کم‌اطمینان)، Imagine Dragons (کم‌اطمینان) |
| Classic Rock | Stevie Nicks / Fleetwood Mac، Don Henley / Eagles، Freddie Mercury / Queen، Brian Johnson / AC-DC، Robert Plant / Led Zeppelin |
| Metal | James Hetfield / Metallica، August Burns Red، Bring Me The Horizon، Lamb of God، David Draiman / Disturbed (قدیمی/کم‌اطمینان) |

نکته: Classic Rock و Metal مستندات کمتری دارند. دیلی‌های میلی‌ثانیه‌ای (مثل ۴۰/۸۰ms) با 1/16 تقریب زده شده‌اند.

## ۴) اگر مشکلی بود

| مشکل | راه‌حل |
|---|---|
| HumRig در لیست Effects نیست | داوینچی را ببند/باز کن؛ Preferences ← System ← Audio Plugins ← Refresh و فعال‌سازی |
| مک می‌گوید «damaged / can't be opened» | دستور `xattr -cr` بالا را اجرا کن |
| داوینچی هنگ می‌کند | یکی از دو فرمت را پاک کن (مثلاً فقط AU بماند) و دوباره امتحان کن |
| صدا ربات‌گونه است | Speed را پایین‌تر و Flex را بالاتر بیاور |

**برای گزارش خطا** این‌ها را بفرست: نسخه‌ی Resolve، نسخه‌ی macOS، چیپ مک (M1/M2/M3 یا Intel)، و پیام خطا/اسکرین‌شات.
