local fontLoaded = false

local healthBar
local healthLabel
local coinLabel
local coins = 0

local volumeSlider
local volumeLabel

local nameField
local submitBtn
local feedbackText

local menuVisible = false
local menuBg
local menuTitle
local playBtn
local quitBtn

function OnStart()
    UI.loadFont("fonts/arial.ttf", 22)
    fontLoaded = true

    healthBar = UI.slider({
        anchor   = UIAnchor.TopLeft,
        x        = 20, y = 20,
        width    = 300, height = 24,
        minValue = 0, maxValue = 100, value = 80,
        color       = { r=0.2, g=0.2, b=0.2, a=1 },
        fillColor   = { r=0.9, g=0.2, b=0.2, a=1 },
        handleColor = { r=1,   g=1,   b=1,   a=1 },
    })

    healthLabel = UI.text({
        anchor = UIAnchor.TopLeft,
        x = 20, y = 50,
        width = 200, height = 28,
        text = "HP: 80 / 100",
        fontSize = 20,
        textColor = { r=1, g=1, b=1, a=1 },
    })

    coinLabel = UI.text({
        anchor = UIAnchor.TopRight,
        x = -160, y = 20,
        width = 140, height = 32,
        text = "Coins: 0",
        fontSize = 22,
        textColor = { r=1, g=0.85, b=0.1, a=1 },
    })

    local collectBtn = UI.button({
        anchor   = UIAnchor.TopRight,
        x        = -160, y = 60,
        width    = 140, height = 36,
        text     = "+ Coin",
        fontSize = 20,
        color      = { r=0.15, g=0.55, b=0.15, a=1 },
        textColor  = { r=1,    g=1,    b=1,    a=1 },
    })
    UI.setOnClick(collectBtn, function()
        coins = coins + 1
        UI.setText(coinLabel, "Coins: " .. coins)
    end)

    volumeSlider = UI.slider({
        anchor   = UIAnchor.BottomLeft,
        x        = 20, y = -60,
        width    = 240, height = 20,
        minValue = 0, maxValue = 1, value = 0.7,
        color       = { r=0.2, g=0.2, b=0.2, a=0.9 },
        fillColor   = { r=0.3, g=0.6, b=1,   a=1   },
        handleColor = { r=1,   g=1,   b=1,   a=1   },
    })
    UI.setOnChanged(volumeSlider, function(v)
        UI.setText(volumeLabel, string.format("Volume: %d%%", math.floor(v * 100)))
    end)

    volumeLabel = UI.text({
        anchor   = UIAnchor.BottomLeft,
        x        = 20, y = -90,
        width    = 200, height = 26,
        text     = "Volume: 70%",
        fontSize = 18,
        textColor = { r=0.8, g=0.8, b=0.8, a=1 },
    })

    nameField = UI.inputField({
        anchor      = UIAnchor.MiddleCenter,
        x           = -200, y = 60,
        width       = 400, height = 44,
        placeholder = "Enter your name...",
        fontSize    = 20,
        color       = { r=0.12, g=0.12, b=0.12, a=0.95 },
        textColor   = { r=1,    g=1,    b=1,    a=1    },
    })

    submitBtn = UI.button({
        anchor    = UIAnchor.MiddleCenter,
        x         = -100, y = 115,
        width     = 200, height = 44,
        text      = "Submit",
        fontSize  = 20,
        color     = { r=0.2, g=0.45, b=0.9, a=1 },
        textColor = { r=1,   g=1,    b=1,   a=1 },
    })
    UI.setOnClick(submitBtn, function()
        local name = UI.getInputText(nameField)
        if name ~= "" then
            UI.setText(feedbackText, "Welcome, " .. name .. "!")
            UI.setColor(feedbackText, { r=0.3, g=1, b=0.4, a=1 })
        else
            UI.setText(feedbackText, "Please enter a name.")
            UI.setColor(feedbackText, { r=1, g=0.4, b=0.3, a=1 })
        end
    end)

    feedbackText = UI.text({
        anchor    = UIAnchor.MiddleCenter,
        x         = -200, y = 170,
        width     = 400, height = 30,
        text      = "",
        fontSize  = 18,
        textColor = { r=1, g=1, b=1, a=1 },
    })

    local menuBtn = UI.button({
        anchor    = UIAnchor.TopCenter,
        x         = -60, y = 20,
        width     = 120, height = 36,
        text      = "Menu",
        fontSize  = 20,
        color     = { r=0.25, g=0.25, b=0.25, a=0.95 },
        textColor = { r=1,    g=1,    b=1,    a=1    },
    })
    UI.setOnClick(menuBtn, function()
        menuVisible = not menuVisible
        UI.setVisible(menuBg,    menuVisible)
        UI.setVisible(menuTitle, menuVisible)
        UI.setVisible(playBtn,   menuVisible)
        UI.setVisible(quitBtn,   menuVisible)
    end)

    menuBg = UI.image({
        anchor  = UIAnchor.MiddleCenter,
        x       = -160, y = -140,
        width   = 320,  height = 280,
        color   = { r=0.08, g=0.08, b=0.08, a=0.92 },
        visible = false,
    })

    menuTitle = UI.text({
        anchor    = UIAnchor.MiddleCenter,
        x         = -140, y = -120,
        width     = 280,  height = 40,
        text      = "PAUSE",
        fontSize  = 28,
        textColor = { r=1, g=1, b=1, a=1 },
        visible   = false,
    })

    playBtn = UI.button({
        anchor    = UIAnchor.MiddleCenter,
        x         = -110, y = -40,
        width     = 220,  height = 50,
        text      = "Resume",
        fontSize  = 22,
        color     = { r=0.15, g=0.55, b=0.15, a=1 },
        textColor = { r=1,    g=1,    b=1,    a=1 },
        visible   = false,
    })
    UI.setOnClick(playBtn, function()
        menuVisible = false
        UI.setVisible(menuBg,    false)
        UI.setVisible(menuTitle, false)
        UI.setVisible(playBtn,   false)
        UI.setVisible(quitBtn,   false)
        Scene.load("scenes/Untitled.scene")
    end)

    quitBtn = UI.button({
        anchor    = UIAnchor.MiddleCenter,
        x         = -110, y = 30,
        width     = 220,  height = 50,
        text      = "Quit",
        fontSize  = 22,
        color     = { r=0.6, g=0.1, b=0.1, a=1 },
        textColor = { r=1,   g=1,   b=1,   a=1 },
        visible   = false,
    })
    UI.setOnClick(quitBtn, function()
        print("Quit requested")
    end)

    print("UIExample loaded")
end

function OnUpdate(dt)
    local hp = UI.getValue(healthBar)
    UI.setText(healthLabel, string.format("HP: %d / 100", math.floor(hp)))
end
