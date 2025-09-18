<div align="center">
  <img src="logo.ico" alt="Trayy logo" width="80" />
  <h1>Trayy</h1>
</div>

<p align="center">
Ever wished you could tell some apps to chill in the system tray? </br>Say hello to Trayy, your desktop's new bouncer!
</br>
</br  >
<img src="https://img.shields.io/github/downloads/alirezagsm/Trayy/total.svg" alt="GitHub downloads"/>
</p>

## 🎯 Features

-   **Send to Tray**: Send your favorite apps to the system tray for a clutter-free workspace!
-   **Hide and Seek Champion**: Enjoy the power to completely hide applications from the taskbar, keeping your desktop neat and tidy.
-   **App Support**: Ideal for Progressive Web Apps (PWAs) and (most) apps that didn't get the memo about system tray support.
-   **Quick Actions**: Quickly send an application to system tray by right clicking on the minimize or close buttons.
-   **Compatible with Windows**: Tested on Windows 10 and 11 with support for toast notifications.
-   **Light as a Feather**: Fully portable and extremely lightweight.

## 🚀 Getting Started

1. Kick things off by downloading `Trayy.zip` and unzipping it to reveal the magic inside!
2. Make sure `Trayy.exe` and `hook.dll` are hanging out in the same folder. Now, Windows Defender might raise a false positive but fear not! Trayy is harmless and also open source ([VirusTotal report](https://www.virustotal.com/gui/file/568369947221e0c41a2d53893644b1b32d9bf28a6efac52dcafdbdce75b06390/detection)). Run the app to get the party started.
3. Time to pick your superpowers:
    - **Send to Tray also when Closed**: Even if you hit the X button, your app will just chill in the tray.
    - **Do not show on Taskbar**: Your app will become the ultimate hide-and-seek champion, staying off the taskbar completely.
4. Now, list out your favorite applications (case-sensitive).
    - Trayy will keep an eye out for any process name as seen in Task Manager that match your entries. For example for `Notepad.exe` you need to add `Notepad` to your lineup.
    - For Web Apps, Trayy will look for browser tab titles that contain your specified string. For example `WhatsApp Web`. Use distinctive keywords to avoid accidental matches!
    - Got an app (like `Thunderbird`) that won't cooperate? Switch its capture mode from `N`ormal to `G`raphical. Trayy will use a special detection method by watching clicks on the top‑right of the titlebar so even tricky programs can be tucked away smoothly!
    - **Heads up:** Universal Windows Platform (UWP) apps (like those from the Microsoft Store) like to do their own thing and aren’t supported by Trayy.
5. Hit Save and BAM! Depending on your settings, your chosen applications will now be tucked away neatly in the system tray.
6. Click on a tray icon to bring its application into the spotlight. If it's already in focus, it'll sneak back into the tray. This way, you can quickly peek at your apps without breaking your workflow!
7. Need an app tucked away for a moment? Right‑click its Minimize button to send it to the tray temporarily. Right‑click the X button to add the app permanently to Trayy's list.

<p align="center">
  <img src="demo.png" alt="GUI demo" style="height:380px; width:auto; vertical-align:top;" align="top"/>
  <img src="demo.gif" alt="Video demo" style="height:375px; width:auto; vertical-align:top;" align="top" autoplay/>
</p>

**Pro Move**: Let Trayy join your startup squad! Add Trayy's shortcut to your `shell:startup` folder, along with all your other favorite apps you want to keep tucked away in the system tray. For WebApps, you can ask your browser to add their shortcuts to your Desktop. Then, simply move those shortcuts into the startup folder. Trayy will chill for a bit, letting the startup process finish, then swoop in to tidy everything up! It's a game changer for messaging and productivity apps!

## 🙏 Acknowledgements

This project is inspired by RBTray.

If Trayy made your workflow smoother, please consider supporting its development! ☕✨

[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/Q5Q21EOKMX)
