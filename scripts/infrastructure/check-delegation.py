#!/usr/bin/env python3
"""Fail CI if compiled delivery grants lose either role or recipient constraints."""
import json
import re
from pathlib import Path
import sys


def check(template):
    modules = [r for r in template['resources'] if r.get('copy', {}).get('name') in ('permissions', 'migrationPermissions')]
    if len(modules) != 2:
        raise ValueError('Both destination and migration grants must be checked')
    for module in modules:
        nested = module['properties']['template']
        variables = nested['variables']
        if variables['administrator'] != 'f58310d9-a9f6-439a-9e8d-f62e7b41a168':
            raise ValueError('Unexpected delegation role')
        grants = nested['resources']
        delegate = next(r for r in grants if "variables('administrator')" in r['properties']['roleDefinitionId'])
        if delegate['name'] != "[guid(resourceGroup().id, parameters('principalId'), variables('administrator'))]":
            raise ValueError('Must restrict the existing assignment in place')
        props = delegate['properties']
        if props.get('conditionVersion') != '2.0' or props.get('condition') != "[if(empty(parameters('bindings')), variables('noDelegation'), variables('limitedDelegation'))]":
            raise ValueError('Missing fail-closed delegation condition')
        expected_none = "(!(ActionMatches{'Microsoft.Authorization/roleAssignments/write'})) AND (!(ActionMatches{'Microsoft.Authorization/roleAssignments/delete'}))"
        if variables['noDelegation'] != expected_none:
            raise ValueError('Groups without runtime roles must deny delegation')
        for key, source in [('writeRules', 'Request'), ('deleteRules', 'Resource')]:
            expected = "[map(parameters('bindings'), lambda('binding', format('(@%s[Microsoft.Authorization/roleAssignments:RoleDefinitionId] GuidEquals {0} AND @%s[Microsoft.Authorization/roleAssignments:PrincipalId] ForAnyOfAnyValues:GuidEquals {{{1}}})', lambdaVariables('binding').role, join(lambdaVariables('binding').principals, ', '))))]" % (source, source)
            if variables[key] != expected:
                raise ValueError('Role and exact recipient must be constrained together')
        expected = "[format('((!(ActionMatches{{''Microsoft.Authorization/roleAssignments/write''}})) OR ({0})) AND ((!(ActionMatches{{''Microsoft.Authorization/roleAssignments/delete''}})) OR ({1}))', join(variables('writeRules'), ' OR '), join(variables('deleteRules'), ' OR '))]"
        if variables['limitedDelegation'] != expected:
            raise ValueError('Both write and delete must be constrained')
        bindings = module['properties']['parameters']['bindings']['value']
        expected_roles = {
            '76199698-9eea-4c19-bc75-cec21354c6b6': ['0', '1'],
            '0a9a7e1f-b9d0-4cc4-a60d-0319b160aaa3': ['0', '1', '2'],
            '7f951dda-4ed3-4680-a7ca-43fe172d538d': ['0', '1', '2', '3'],
            '4633458b-17de-408a-b874-0445c86b69e6': ['1'],
            'b86a8fe4-44ce-4948-aee5-eccb2c155cd7': ['3'],
        }
        entries = bindings.split("createObject('role', '")[1:]
        actual = {entry.split("'", 1)[0]: re.findall(r"createArray\('api', 'worker', 'factory', 'bootstrap'\)\[(\d)\]", entry) for entry in entries}
        if len(entries) != 5 or actual != expected_roles:
            raise ValueError('Compiled role/recipient matrix differs from approved runtime grants')
        if '.outputs.principalId' in bindings or 'tempo-infrastructure-delivery' in bindings:
            raise ValueError('Delivery identity cannot receive delegated runtime roles')
        if module['copy']['name'] == 'migrationPermissions' and module.get('condition') != "[parameters('includeMigrationSources')]":
            raise ValueError('Migration grants must be explicitly enabled')
    print('PASS: compiled delegation restricts existing grants, both actions and exact recipients')


if __name__ == '__main__':
    check(json.loads(Path(sys.argv[1]).read_text()))
