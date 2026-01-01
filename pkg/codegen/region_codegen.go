package codegen

import (
	"fmt"
	"strings"

	"purple_go/pkg/analysis"
	"purple_go/pkg/ast"
)

// GenerateRegionLet generates a let with region allocation.
func (g *CodeGenerator) GenerateRegionLet(bindings []struct {
	sym *ast.Value
	val *ast.Value
}, body *ast.Value, region *analysis.Region) string {
	var sb strings.Builder

	regionID := 0
	if region != nil {
		regionID = region.ID
	}

	sb.WriteString(fmt.Sprintf("Region* _rgn_%d = region_enter();\n", regionID))
	sb.WriteString("({\n")

	for _, bi := range bindings {
		varName := bi.sym.Str
		valStr := g.ValueToCExpr(bi.val)

		sb.WriteString(fmt.Sprintf("    Obj* %s = %s;\n", varName, valStr))
		sb.WriteString(fmt.Sprintf("    RegionObj* _rgnobj_%s = region_alloc(%s, (void (*)(void*))free_obj);\n", varName, varName))
	}

	bodyStr := g.ValueToCExpr(body)
	sb.WriteString(fmt.Sprintf("    Obj* _res = %s;\n", bodyStr))

	sb.WriteString("    region_exit();\n")
	sb.WriteString("    _res;\n")
	sb.WriteString("})")

	return sb.String()
}
