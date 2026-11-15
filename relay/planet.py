"""Read-only atlas and request-isolated regional terrain resolution."""
from pathlib import Path
import tempfile

from flask import Blueprint, g, jsonify, request, send_from_directory

from pipeline.planet import RegionError, atlas, materialize_region, parse_region


def register_planet(app,default_root):
    blueprint=Blueprint('planet',__name__)
    @blueprint.get('/planet.js')
    def javascript():
        return send_from_directory(Path(__file__).resolve().parent.parent/'web','planet.js')

    @blueprint.get('/api/mars/atlas')
    def get_atlas():
        return jsonify(atlas())

    @app.errorhandler(RegionError)
    def invalid_region(error):
        return jsonify(error=str(error)),400

    @app.teardown_request
    def cleanup_region(error):
        directory=g.pop('planet_temporary',None)
        if directory is not None:directory.cleanup()

    def resolve():
        region=request.args.get('region','jezero')
        if region=='jezero':return default_root
        if not hasattr(g,'planet_root'):
            lat,lon=parse_region(region)
            temporary=tempfile.TemporaryDirectory(prefix='mars-region-')
            g.planet_temporary=temporary
            g.planet_root=Path(temporary.name)
            materialize_region(g.planet_root,lat,lon)
        return g.planet_root

    app.register_blueprint(blueprint)
    return resolve
