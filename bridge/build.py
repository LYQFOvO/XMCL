#!/usr/bin/env python3
"""Build the MIT-licensed KAMUCL Bridge with pinned, verified compile dependencies."""
from pathlib import Path
import hashlib, os, shutil, ssl, subprocess, urllib.request
root = Path(__file__).resolve().parent
cache, classes, dist = root / '.cache', root / 'build/classes', root / 'dist'
deps = [
('https://maven.fabricmc.net/net/fabricmc/fabric-loader/0.19.5/fabric-loader-0.19.5.jar', '93044e4dd46de5d8136701292f05e868da096d2c9fddb4793e4fdbcc63efc695'),
('https://repo.maven.apache.org/maven2/com/google/code/gson/gson/2.14.0/gson-2.14.0.jar', '2cbd119bf1961c28788310963dc80ba65f58cdeec1dd139c8bdb1240faa2c36f')]
cache.mkdir(parents=True, exist_ok=True)
classpath = []
for url, expected in deps:
    path = cache / url.rsplit('/', 1)[1]
    if not path.exists() or hashlib.sha256(path.read_bytes()).hexdigest() != expected:
        context = ssl.create_default_context(cafile='/etc/ssl/cert.pem') if Path('/etc/ssl/cert.pem').exists() else ssl.create_default_context()
        with urllib.request.urlopen(url, timeout=30, context=context) as response:
            data = response.read(8 * 1024 * 1024)
        if hashlib.sha256(data).hexdigest() != expected:
            raise RuntimeError('Compile dependency SHA256 mismatch: ' + path.name)
        path.write_bytes(data)
    classpath.append(str(path))
if classes.exists(): shutil.rmtree(classes)
classes.mkdir(parents=True); dist.mkdir(exist_ok=True)
def tool(name):
    return str(Path(os.environ['JAVA_HOME'])/'bin'/name) if os.environ.get('JAVA_HOME') else name
subprocess.run([tool('javac'), '-encoding', 'UTF-8', '--release', '17', '-cp', os.pathsep.join(classpath), '-d', str(classes), *map(str, (root/'src').rglob('*.java'))], check=True)
shutil.copy2(root/'src/fabric.mod.json', classes/'fabric.mod.json')
shutil.copy2(root.parent/'licenses/kamucl/LICENSE', classes/'LICENSE')
subprocess.run([tool('jar'), 'cf', str(dist/'kamucl-bridge-1.0.1.jar'), '-C', str(classes), '.'], check=True)
print(dist/'kamucl-bridge-1.0.1.jar')
