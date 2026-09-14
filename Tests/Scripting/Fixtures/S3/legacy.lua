local Script = { ticks = 0 }
function Script:OnUpdate(dt)
    self.ticks = self.ticks + dt
end
return Script
