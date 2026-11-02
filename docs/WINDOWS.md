# MonteCalc on Windows

## Installing the extension

Requirements: **LibreOffice 7.6 or later, 64-bit** (the extension is built for
64-bit Windows).

1. Get `MonteCalc-windows_x86_64.oxt`: from the latest GitHub Actions run
   (*Actions → build → Artifacts*) or the release assets.
2. Open LibreOffice, then go to **Tools → Extension Manager… → Add…**, pick the
   `.oxt` file and accept.
   - Alternatively, double-click the `.oxt` file, or run from a command prompt:
     `"C:\Program Files\LibreOffice\program\unopkg.com" add MonteCalc-windows_x86_64.oxt`
3. Restart LibreOffice, closing it completely, including the Quickstarter in
   the system tray.
4. Open `examples\business_case.ods`. The **MonteCalc** menu and toolbar appear
   in Calc; start with **Simulation → Start**.

**AI mentor:** put your Anthropic API key in
`%USERPROFILE%\Documents\MC_FLO_API.txt`, or type it into the mentor window.
On Windows the mentor uses WinHTTP, which is part of Windows and follows the
system proxy settings.

To uninstall: **Tools → Extension Manager**, select MonteCalc, then **Remove**.

## Building it yourself

The easiest way is to push the repository to GitHub. The workflow in
`.github/workflows/build.yml` builds the Windows and Linux extensions, runs the
tests and attaches the `.oxt` files to the run.

To build locally on Windows:

1. Install **Visual Studio 2022** (or Build Tools) with *Desktop development
   with C++*, **CMake** and **Python 3**.
2. Install **LibreOffice (64-bit)** and the matching **LibreOffice SDK**. Both
   are on https://www.libreoffice.org/download/ (the SDK is listed under
   *Development Kit*).
3. In an **x64 Native Tools Command Prompt for VS 2022**, run:

   ```bat
   set PATH=C:\Program Files\LibreOffice\program;%PATH%
   cmake -S . -B build-win -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release ^
         -DLO_DIR="C:/Program Files/LibreOffice" -DLO_SDK_DIR="C:/Program Files/LibreOffice/sdk"
   cmake --build build-win
   ctest --test-dir build-win
   ```

   The extension is written to `build-win\MonteCalc-windows_x86_64.oxt`.

The SDK tools (`unoidl-write`, `cppumaker`) need LibreOffice's `program`
folder on `PATH`. Adjust `LO_SDK_DIR` if the SDK was installed elsewhere.

## What was verified for Windows

- The core library and its 1,400 unit-test checks were cross-compiled as a
  Windows x64 executable and passed.
- The mentor's WinHTTP code was compiled for Windows and made a real HTTPS
  request to the Anthropic API.
- The UNO add-in itself (`montecalc.dll`) is compiled with Visual Studio by the
  workflow above, against the official LibreOffice SDK for Windows.
