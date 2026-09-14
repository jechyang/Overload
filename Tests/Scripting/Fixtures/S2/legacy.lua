local script = { speed = 2.0, awakeSpeed = 0.0, started = false, label = "default", ticks = 0.0,
    flag = false, awakeFlag = false, target = Actor(), targetName = "", prefab = Prefab() }
function script:OnAwake()
    self.awakeSpeed = self.speed
    self.owner:SetName(self.label)
    self.awakeFlag = self.flag
    if self.target then self.targetName = self.target:GetName() end
end
function script:OnStart() self.started = true end
function script:OnUpdate(dt) self.ticks = self.ticks + self.speed * dt end
return script
