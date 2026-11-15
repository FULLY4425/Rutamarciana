"""Run the GitHub source ZIP with: python iniciar.py (Python 3.10+)."""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import venv

ROOT = Path(__file__).resolve().parent
ENV = ROOT / '.server-venv'
REQUIREMENTS = ROOT / 'requirements-server.txt'
DATA_FILES = ('routes.json', 'dem.npy', 'slope.npy', 'rough.npy')


def options(argv=None):
    parser = argparse.ArgumentParser(description='Ruta Marciana · servidor local')
    parser.add_argument('--host', default='0.0.0.0', help='Interfaz de escucha (default: todas)')
    parser.add_argument('--port', type=int, default=8080, help='Puerto (default: 8080)')
    terrain = parser.add_mutually_exclusive_group()
    terrain.add_argument('--data', type=Path, help='Carpeta de terreno existente')
    terrain.add_argument('--demo', action='store_true', help='Usar terreno sintético en lugar del Jezero incluido')
    parser.add_argument('--check', action='store_true', help='Preparar y verificar sin iniciar el servidor')
    args = parser.parse_args(argv)
    if not 1 <= args.port <= 65535:
        parser.error('El puerto debe estar entre 1 y 65535.')
    if args.data is None:
        bundled = ROOT / 'assets' / 'jezero'
        args.data = ROOT / 'data' / 'demo' if args.demo else (bundled if (bundled / 'routes.json').exists() else ROOT / 'data' / 'out')
    if not args.data.is_absolute():
        args.data = ROOT / args.data
    return args


def environment_python():
    return ENV / ('Scripts/python.exe' if os.name == 'nt' else 'bin/python')


def bootstrap():
    """Install only in the app's isolated environment, and only on input changes."""
    python = environment_python()
    if not python.exists():
        print('Preparando entorno Python aislado…', flush=True)
        venv.EnvBuilder(with_pip=True).create(ENV)
    digest = hashlib.sha256(REQUIREMENTS.read_bytes()).hexdigest()
    stamp = ENV / '.requirements-sha256'
    if not stamp.exists() or stamp.read_text().strip() != digest:
        print('Instalando dependencias. La primera vez se requiere internet…', flush=True)
        subprocess.run([str(python), '-m', 'pip', 'install', '-r', str(REQUIREMENTS)], check=True)
        stamp.write_text(digest, encoding='utf-8')
    return python


def prepare_data(directory):
    """Generate a demo only when no terrain data exists; never replace user data."""
    existing = [name for name in DATA_FILES if (directory / name).exists()]
    if not existing:
        # A nonempty directory might contain a real dataset with different names.
        if directory.exists() and any(directory.iterdir()):
            raise ValueError(f'La carpeta {directory} contiene archivos pero no un terreno compatible. Usa --data con otra carpeta vacía o con un build completo.')
        print('Generando DEMO SINTÉTICA del proyecto (no es el DEM real de Jezero)…', flush=True)
        subprocess.run([sys.executable, '-m', 'pipeline.build', '--synth', '--rows', '240', '--out', str(directory)], cwd=ROOT, check=True)
    elif len(existing) != len(DATA_FILES):
        missing = ', '.join(name for name in DATA_FILES if name not in existing)
        raise ValueError(f'Terreno incompleto: faltan {missing}. No se han reemplazado datos. Regenera el build o usa --data con otra carpeta vacía.')
    from relay.app import create_app
    client = create_app(str(directory)).test_client()
    response = client.get('/api/routes')
    if response.status_code != 200:
        raise ValueError(f'Terreno incompatible: {response.get_json()}. No se han reemplazado datos.')
    response = client.get('/api/terrain')
    if response.status_code != 200:
        raise ValueError('No se pudo cargar la malla del terreno.')
    return create_app(str(directory))


def run(args):
    from waitress import serve
    app = prepare_data(args.data)
    if args.check:
        print('LISTO: servidor, rutas y terreno verificados.', flush=True)
        return 0
    local = f'http://127.0.0.1:{args.port}/'
    print(f'\nRUTA MARCIANA\nEn este servidor: {local}', flush=True)
    if args.host == '0.0.0.0':
        print(f'En otro equipo: http://IP-DEL-SERVIDOR:{args.port}/\nConsulta la IP con ipconfig (Windows) o hostname -I (Linux).', flush=True)
    print('Deja esta ventana abierta. Ctrl+C para detener.\n', flush=True)
    serve(app, host=args.host, port=args.port, threads=4)
    return 0


def main(argv=None):
    if sys.version_info < (3, 10):
        print('Se necesita Python 3.10 o posterior. Se recomienda Python 3.11 o 3.12.', file=sys.stderr)
        return 1
    args = options(argv)
    try:
        if Path(sys.prefix).resolve() != ENV.resolve():
            python = bootstrap()
            return subprocess.call([str(python), str(Path(__file__).resolve()), *(sys.argv[1:] if argv is None else argv)], cwd=ROOT)
        return run(args)
    except KeyboardInterrupt:
        print('\nServidor detenido.')
        return 0
    except (OSError, ValueError, subprocess.CalledProcessError, ImportError) as error:
        print(f'\nNo se pudo iniciar: {error}\nRevisa la conexión a internet en la primera instalación, los permisos de la carpeta y que el puerto esté libre. En Linux, si falta venv, instala el paquete python3-venv.', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
