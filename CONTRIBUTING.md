# Contributing to CapApp

Thanks for your interest in improving CapApp. Bug reports, feature suggestions,
documentation improvements, and pull requests are welcome.

## Before opening an issue

- Search existing issues to avoid duplicates.
- Include your Windows version, CapApp version, capture device, and steps to
  reproduce a bug.
- Remove personal information from logs and screenshots before sharing them.
- For security vulnerabilities, follow [SECURITY.md](SECURITY.md) instead of
  filing a public issue.

## Building and testing

CapApp currently targets Windows and uses MSVC, CMake, and Ninja. From a Visual
Studio Developer PowerShell, configure and build with:

```powershell
cmake --preset ninja-release
cmake --build --preset ninja-release
ctest --preset ninja-release
```

Run the relevant tests after changes. For changes affecting capture behavior,
include the capture device and negotiated mode used during manual testing when
available. Hardware-dependent results are useful but are not required for
documentation-only changes.

## Pull requests

- Keep changes focused and explain the user-visible behavior or problem they
  address.
- Update documentation and tests when behavior changes.
- Use clear commit messages; the project prefers Conventional Commits such as
  `fix: report capture errors` or `docs: explain native-size mode`.
- Describe the tests you ran and note any hardware testing that was not
  possible.
- Do not include credentials, private device data, or unrelated generated
  build files.

By submitting a contribution, you agree that it may be distributed under the
project's [MIT License](LICENSE).
