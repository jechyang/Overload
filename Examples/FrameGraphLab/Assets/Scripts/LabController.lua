local lab = { animate = true, shadows = true, reflections = true, bloom = true,
    exposure = true, fxaa = true, glass = true, gallery = true,
    progressiveCapture = true, exposureProgressive = true, speed = 1.0 }

function lab:OnStart()
    local scene = Scenes.GetCurrentScene()
    self._sun = scene:FindActorByName("01 Shadow Sun"):GetDirectionalLight()
    self._post = scene:FindActorByName("04 Post Process"):GetPostProcessStack()
    self._localProbe = scene:FindActorByName("06 Local Probe - Progressive")
    self._globalProbe = scene:FindActorByName("05 Global Probe - Once")
    self._ring = scene:FindActorByName("Kinetic Ring"):GetTransform()
    self._orb = scene:FindActorByName("Orbiting Amber"):GetTransform()
    self._glass = scene:FindActorByName("30 Transparency")
    self._gallery = scene:FindActorByName("50 Shared Material Gallery")
    self._skins = {
        scene:FindActorByName("Animated Copper"):GetSkinnedMeshRenderer(),
        scene:FindActorByName("Animated Chrome"):GetSkinnedMeshRenderer()
    }
    self._time = 0
    self._applied = {}
    self._acceptKeys = true
    Debug.LogInfo("FrameGraphLab: 1 shadow | 2 reflection | 3 bloom | 4 exposure | 5 FXAA | 6 probe speed | T glass | G gallery | P motion | R recapture")
    self:ApplySettings()
end

function lab:ApplySettings()
    local function changed(name)
        if self._applied[name] == self[name] then return false end
        self._applied[name] = self[name]
        return true
    end
    if changed("shadows") then self._sun:SetCastShadow(self.shadows) end
    if changed("reflections") then
        self._localProbe:SetActive(self.reflections)
        self._globalProbe:SetActive(self.reflections)
        if self.reflections then self._globalProbe:GetReflectionProbe():RequestCapture(true) end
    end
    if changed("progressiveCapture") then
        self._localProbe:GetReflectionProbe():SetCaptureSpeed(self.progressiveCapture and ReflectionProbeCaptureSpeed.ONE_FACE or ReflectionProbeCaptureSpeed.SIX_FACES)
    end
    if changed("bloom") then
        local settings = self._post:GetBloomSettings()
        settings.Enabled = self.bloom
        self._post:SetBloomSettings(settings)
    end
    local exposureChanged = changed("exposure")
    local progressiveChanged = changed("exposureProgressive")
    if exposureChanged or progressiveChanged then
        local settings = self._post:GetAutoExposureSettings()
        settings.Enabled = self.exposure
        settings.Progressive = self.exposureProgressive
        self._post:SetAutoExposureSettings(settings)
    end
    if changed("fxaa") then
        local settings = self._post:GetFXAASettings()
        settings.Enabled = self.fxaa
        self._post:SetFXAASettings(settings)
    end
    if changed("glass") then self._glass:SetActive(self.glass) end
    if changed("gallery") then self._gallery:SetActive(self.gallery) end
    if changed("animate") then
        for _, skin in ipairs(self._skins) do
            if self.animate then skin:Play() else skin:Pause() end
        end
    end
end

function lab:HandleKeys()
    local bindings = {
        {Key.ALPHA_1, "shadows"}, {Key.ALPHA_2, "reflections"},
        {Key.ALPHA_3, "bloom"}, {Key.ALPHA_4, "exposure"},
        {Key.ALPHA_5, "fxaa"}, {Key.ALPHA_6, "progressiveCapture"},
        {Key.T, "glass"}, {Key.G, "gallery"}, {Key.P, "animate"}
    }
    for _, binding in ipairs(bindings) do
        if Inputs.GetKeyDown(binding[1]) then
            self[binding[2]] = not self[binding[2]]
            Debug.LogInfo(binding[2] .. " = " .. tostring(self[binding[2]]))
        end
    end
    if Inputs.GetKeyDown(Key.R) then
        self._globalProbe:GetReflectionProbe():RequestCapture(true)
        self._localProbe:GetReflectionProbe():RequestCapture(true)
        Debug.LogInfo("Reflection recapture requested")
    end
end

function lab:OnUpdate(dt)
    if self._acceptKeys then self:HandleKeys() end
    self:ApplySettings()
    if self.animate then
        self._time = self._time + dt * self.speed
        self._ring:SetLocalRotation(Quaternion.new(Vector3.new(90, self._time * 24, self._time * 12)))
        self._orb:SetLocalPosition(Vector3.new(3 * math.cos(self._time), 2.8 + .6 * math.sin(self._time * 2), -1 + 3 * math.sin(self._time)))
    end
end
return lab
