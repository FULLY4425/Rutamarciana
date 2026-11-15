"""Field feature assets and the region-scoped science catalog."""
import json
from pathlib import Path
from flask import Blueprint,jsonify,request,send_from_directory,abort

ROOT=Path(__file__).resolve().parent.parent

def register_field(app):
    blueprint=Blueprint('field',__name__)
    @blueprint.get('/field/<name>.js')
    def script(name):
        if name not in {'points','operations','actors','navigator','texture','mission','chemistry','mobile','map-touch'}:abort(404)
        return send_from_directory(ROOT/'web',name+'.js')
    @blueprint.get('/api/science-targets')
    def targets():
        region=request.args.get('region','jezero')
        records=json.loads((ROOT/'assets/glen-torridon/targets.json').read_text()) if region=='glen-torridon' else []
        return jsonify(region=region,targets=records,source='https://pds-geosciences.wustl.edu/msl/urn-nasa-pds-msl_gt_diagenesis_supplement/',note='The 718 ChemCam records apply to Glen Torridon/Gale only.')
    @blueprint.get('/api/chemistry')
    def chemistry():
        if request.args.get('region','jezero')!='glen-torridon':
            return jsonify(observations=[],note='Quantitative ChemCam coverage is limited to matched Glen Torridon targets.')
        return send_from_directory(ROOT/'assets/glen-torridon','chemistry.json')
    app.register_blueprint(blueprint)
