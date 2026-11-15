#!/usr/bin/env python3
"""Build a reusable shortcut. No device address, token, or user data is embedded.
Action IDs and input attachment shape come from a native macOS Shortcuts export.
Sign the output with Apple's `shortcuts sign --mode anyone` before distributing.
"""
import plistlib
from pathlib import Path
root=Path(__file__).resolve().parents[1]
loc='1AC56C8D-2872-4AD8-9AB3-E79071DD0EC1'
lat='F5F38854-B703-4484-B15D-406735E18D18'
lon='1841BE8A-291C-47B4-9C4F-9119782E90D0'
def ref(uuid,name):return {'OutputUUID':uuid,'Type':'ActionOutput','OutputName':name}
def action(identifier,params):return {'WFWorkflowActionIdentifier':identifier,'WFWorkflowActionParameters':params}
actions=[action('is.workflow.actions.getcurrentlocation',{'UUID':loc})]
for prop,uuid in [('Latitude',lat),('Longitude',lon)]:
 actions.append(action('is.workflow.actions.properties.locations',{'UUID':uuid,'WFContentItemPropertyName':prop,'WFInput':{'Value':ref(loc,'Current Location'),'WFSerializationType':'WFTextTokenAttachment'}}))
actions.append(action('is.workflow.actions.gettext',{'WFTextActionText':{'WFSerializationType':'WFTextTokenString','Value':{'string':'WSLOC1\n\ufffc\n\ufffc','attachmentsByRange':{'{7, 1}':ref(lat,'Latitude'),'{9, 1}':ref(lon,'Longitude')}}}}))
workflow={'WFWorkflowName':'Weather Station Location','WFWorkflowMinimumClientVersion':900,'WFWorkflowMinimumClientVersionString':'900','WFWorkflowClientVersion':'4407','WFWorkflowIcon':{'WFWorkflowIconStartColor':431817727,'WFWorkflowIconGlyphNumber':61440},'WFWorkflowActions':actions,'WFWorkflowHasOutputFallback':False,'WFWorkflowOutputContentItemClasses':['WFStringContentItem'],'WFWorkflowInputContentItemClasses':[],'WFWorkflowImportQuestions':[],'WFWorkflowTypes':['WFWorkflowTypeShowInSearch'],'WFQuickActionSurfaces':[],'WFWorkflowHasShortcutInputVariables':False}
out=root/'companion/shortcut/Weather Station Location.unsigned.shortcut'
out.write_bytes(plistlib.dumps(workflow,fmt=plistlib.FMT_BINARY,sort_keys=False))
print(out)
