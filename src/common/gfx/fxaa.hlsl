float4 texel : register(c0);   // 1/width, 1/height, width, height
sampler2D frame : register(s0);

struct VS_OUT { float4 pos : POSITION; float2 uv : TEXCOORD0; };
VS_OUT vsmain(float4 pos : POSITION, float2 uv : TEXCOORD0) {
    VS_OUT o;
    o.pos = float4(pos.x - texel.x, pos.y + texel.y, 0, 1);   // D3D9's half-pixel offset
    o.uv = uv;
    return o;
}

float4 Tap(float2 uv) { return tex2Dlod(frame, float4(uv, 0, 0)); }
float Luma(float2 uv) { return dot(Tap(uv).rgb, float3(0.299, 0.587, 0.114)); }

float4 psmain(float2 uv : TEXCOORD0) : COLOR {
    const float edgeThreshold = 0.125, edgeThresholdMin = 0.0312, subpixelQuality = 0.75;
    float2 px = texel.xy;
    float4 centre = Tap(uv);
    float lM = dot(centre.rgb, float3(0.299, 0.587, 0.114));
    float lN = Luma(uv + float2(0, -px.y)), lS = Luma(uv + float2(0, px.y));
    float lW = Luma(uv + float2(-px.x, 0)), lE = Luma(uv + float2(px.x, 0));
    float lMax = max(lM, max(max(lN, lS), max(lW, lE)));
    float lMin = min(lM, min(min(lN, lS), min(lW, lE)));
    float range = lMax - lMin;
    if (range < max(edgeThresholdMin, lMax * edgeThreshold))
        return centre;

    float lNW = Luma(uv - px), lSE = Luma(uv + px);
    float lNE = Luma(uv + float2(px.x, -px.y)), lSW = Luma(uv + float2(-px.x, px.y));
    float edgeH = abs(lNW + lSW - 2 * lW) + 2 * abs(lN + lS - 2 * lM) + abs(lNE + lSE - 2 * lE);
    float edgeV = abs(lNW + lNE - 2 * lN) + 2 * abs(lW + lE - 2 * lM) + abs(lSW + lSE - 2 * lS);
    bool horizontal = edgeH >= edgeV;   // the edge runs across; the step is up or down

    float l1 = horizontal ? lN : lW, l2 = horizontal ? lS : lE;
    float grad1 = l1 - lM, grad2 = l2 - lM;
    bool towards1 = abs(grad1) >= abs(grad2);
    float gradScaled = 0.25 * max(abs(grad1), abs(grad2));
    float stepLength = horizontal ? px.y : px.x;
    float localAverage;
    if (towards1) { stepLength = -stepLength; localAverage = 0.5 * (l1 + lM); }
    else          { localAverage = 0.5 * (l2 + lM); }

    // Walk both ways along the edge, from halfway across it, until the luma leaves the edge's average.
    float2 onEdge = uv;
    if (horizontal) onEdge.y += 0.5 * stepLength; else onEdge.x += 0.5 * stepLength;
    float2 along = horizontal ? float2(px.x, 0) : float2(0, px.y);
    float2 uv1 = onEdge - along, uv2 = onEdge + along;
    float end1 = Luma(uv1) - localAverage, end2 = Luma(uv2) - localAverage;
    bool done1 = abs(end1) >= gradScaled, done2 = abs(end2) >= gradScaled;
    const float stride[8] = { 1.0, 1.0, 1.0, 1.5, 2.0, 2.0, 4.0, 8.0 };
    [unroll] for (int i = 0; i < 8; i++) {
        if (!done1) { uv1 -= along * stride[i]; end1 = Luma(uv1) - localAverage; done1 = abs(end1) >= gradScaled; }
        if (!done2) { uv2 += along * stride[i]; end2 = Luma(uv2) - localAverage; done2 = abs(end2) >= gradScaled; }
    }

    float dist1 = horizontal ? uv.x - uv1.x : uv.y - uv1.y;
    float dist2 = horizontal ? uv2.x - uv.x : uv2.y - uv.y;
    bool nearer1 = dist1 < dist2;
    float edgeOffset = 0.5 - min(dist1, dist2) / (dist1 + dist2);
    bool centreDarker = lM < localAverage;
    if (((nearer1 ? end1 : end2) < 0) == centreDarker)
        edgeOffset = 0;   // the nearer end continues the centre's side of the edge: nothing to blend

    float average = (2 * (lN + lS + lW + lE) + (lNW + lNE + lSW + lSE)) / 12;
    float sub = saturate(abs(average - lM) / range);
    sub = (-2 * sub + 3) * sub * sub;
    float offset = max(edgeOffset, sub * sub * subpixelQuality);

    float2 finalUv = uv;
    if (horizontal) finalUv.y += offset * stepLength; else finalUv.x += offset * stepLength;
    return float4(Tap(finalUv).rgb, centre.a);
}
