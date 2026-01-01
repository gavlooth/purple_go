package codegen

import (
	"fmt"
	"strings"

	"purple_go/pkg/analysis"
)

// DPSCodeGenerator generates DPS function variants.
type DPSCodeGenerator struct {
	analyzer *analysis.DPSAnalyzer
}

// NewDPSCodeGenerator creates a new DPS code generator.
func NewDPSCodeGenerator(analyzer *analysis.DPSAnalyzer) *DPSCodeGenerator {
	return &DPSCodeGenerator{analyzer: analyzer}
}

// GenerateDPSVariant generates a DPS version of a function.
func (g *DPSCodeGenerator) GenerateDPSVariant(candidate *analysis.DPSCandidate) string {
	var sb strings.Builder

	params := []string{"Obj** _dest"}
	for _, p := range candidate.Params {
		params = append(params, "Obj* "+p)
	}
	sb.WriteString(fmt.Sprintf("void %s_dps(%s) {\n", candidate.Name, strings.Join(params, ", ")))

	g.generateDPSBody(&sb, candidate)

	sb.WriteString("}\n")
	return sb.String()
}

func (g *DPSCodeGenerator) generateDPSBody(sb *strings.Builder, candidate *analysis.DPSCandidate) {
	if candidate.IsTailCall {
		g.generateTailRecursiveDPS(sb, candidate)
		return
	}
	g.generateSimpleDPS(sb, candidate)
}

func (g *DPSCodeGenerator) generateSimpleDPS(sb *strings.Builder, _ *analysis.DPSCandidate) {
	sb.WriteString("    /* TODO: DPS lowering */\n")
	sb.WriteString("    *_dest = NULL;\n")
}

func (g *DPSCodeGenerator) generateTailRecursiveDPS(sb *strings.Builder, _ *analysis.DPSCandidate) {
	sb.WriteString("    /* TODO: tail-recursive DPS lowering */\n")
	sb.WriteString("    *_dest = NULL;\n")
}

// GenerateAllDPSVariants generates DPS variants for all candidates.
func (g *DPSCodeGenerator) GenerateAllDPSVariants() string {
	var sb strings.Builder

	sb.WriteString("/* ========== DPS Function Variants ========== */\n\n")
	for _, candidate := range g.analyzer.Candidates {
		sb.WriteString(g.GenerateDPSVariant(candidate))
		sb.WriteString("\n")
	}

	return sb.String()
}
