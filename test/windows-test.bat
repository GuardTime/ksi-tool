RMDIR /S /Q test\out\sign
RMDIR /S /Q test\out\extend
mkdir test\out\sign
mkdir test\out\extend

shelltest test\test_suites\sign.test --with=bin\ksi.exe -- -j1
shelltest test\test_suites\static-sign.test --with=bin\ksi.exe -- -j1
shelltest test\test_suites\sign-verify.test --with=bin\ksi.exe -- -j1
shelltest test\test_suites\extend.test --with=bin\ksi.exe -- -j1
shelltest test\test_suites\extend-verify.test --with=bin\ksi.exe -- -j1
shelltest test\test_suites\static-verify.test --with=bin\ksi.exe -- -j1
shelltest test\test_suites\static-sign-verify.test --with=bin\ksi.exe -- -j1
