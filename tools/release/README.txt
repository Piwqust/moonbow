Moonbow
=======
A fast, quiet video and music player for Windows 10 and 11.
Быстрый и тихий видео- и музыкальный плеер для Windows 10 и 11.

https://github.com/Piwqust/moonbow


ENGLISH

Install
1. Run Moonbow-windows-x64-setup.exe. No administrator rights are needed.
   The build is not code-signed yet: if Windows says "Windows protected your PC",
   press "More info", then "Run anyway".
2. Press "Install". Moonbow goes to %LOCALAPPDATA%\Programs\Moonbow and to the Start menu.
   Videos open in Moonbow by default; turn that off under "Install options".
Portable: unpack Moonbow-windows-x64.zip into any folder and run Moonbow.exe.

Update and go back
- Run the newer installer. It shows what changed; your settings and library stay.
- The two previous versions stay on the computer: About Moonbow > Versions on this
  computer > Go back to it.
- Settings and the library live in %APPDATA%\Moonbow and survive updates.

If something goes wrong
1. Settings > General > Diagnostics > Logs and crash reports > Open folder.
2. Open an issue at https://github.com/Piwqust/moonbow/issues and attach those files,
   with what you did, what you expected and what happened.
For a file that plays wrong, turn on "Detailed playback log" there, open the file again
and attach mpv.log. Moonbow sends nothing by itself.

License: GNU GPL v3 or later (LICENSE.txt). Third-party parts: THIRD_PARTY_NOTICES.md.


РУССКИЙ

Установка
1. Запустите Moonbow-windows-x64-setup.exe. Права администратора не нужны.
   Подписи кода у сборки пока нет: если Windows пишет «Система Windows защитила ваш
   компьютер», нажмите «Подробнее», затем «Выполнить в любом случае».
2. Нажмите «Установить». Moonbow встанет в %LOCALAPPDATA%\Programs\Moonbow и в меню «Пуск».
   Видео будут открываться в Moonbow; выключить это можно в «Параметрах установки».
Без установки: распакуйте Moonbow-windows-x64.zip в любую папку и запустите Moonbow.exe.

Обновление и возврат
- Запустите установщик новой версии. Он покажет, что изменилось; настройки и медиатека
  останутся.
- Две прошлые версии остаются на компьютере: О программе > Версии на этом компьютере >
  Вернуться на неё.
- Настройки и медиатека лежат в %APPDATA%\Moonbow и при обновлении не теряются.

Если что-то не так
1. Настройки > Общие > Диагностика > Журналы и отчёты о сбоях > Открыть папку.
2. Создайте issue на https://github.com/Piwqust/moonbow/issues и приложите эти файлы:
   что делали, чего ждали и что случилось.
Если неправильно играет конкретный файл, включите там же «Подробный журнал
воспроизведения», откройте файл ещё раз и приложите mpv.log. Сам Moonbow ничего не отправляет.

Лицензия: GNU GPL v3 или новее (LICENSE.txt). Сторонние части: THIRD_PARTY_NOTICES.md.
